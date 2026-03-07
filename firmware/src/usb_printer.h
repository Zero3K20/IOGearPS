/*
 * usb_printer.h — USB Printer Class driver interface for the GPSU21.
 *
 * This module provides bi-directional communication with a USB printer
 * attached to the MT7688's on-chip USB host port.
 *
 * USB Printer Class (Class 7, Protocol 2) supports two data paths:
 *   Forward path:  Bulk OUT endpoint — host writes print data to printer
 *   Backward path: Bulk IN  endpoint — printer sends status/response data
 *                                       back to host (the "bi-directional" leg)
 *
 * Additionally, class-specific control requests expose:
 *   GET_DEVICE_ID   — IEEE 1284 Device ID string (make, model, command sets)
 *   GET_PORT_STATUS — 1-byte IEEE 1284 status (online, paper, error)
 *   SOFT_RESET      — class-specific printer reset
 *
 * The live printer status is stored in the global g_printer_status and is
 * updated by usb_printer_update_status(), which must be called periodically
 * from the status thread.  The status is then consumed by:
 *   - ipp_server.c  — Get-Printer-Attributes returns real printer-state
 *   - lpr.c         — queue-state response reflects paper/offline state
 *   - httpd.c       — /api/printer_status endpoint for the web UI
 *
 * References:
 *   - Universal Serial Bus Specification, Revision 2.0
 *   - USB Printer Class Definition for Printing Devices, Release 1.1
 *   - Enhanced Host Controller Interface (EHCI) Specification, Release 1.0
 *   - MT7628AN/MT7688 Datasheet, MediaTek Inc.
 */

#ifndef USB_PRINTER_H
#define USB_PRINTER_H

#include <stdint.h>
#include <stddef.h>
#include "rtos.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * IEEE 1284 port-status bits returned by GET_PORT_STATUS (USB Printer Class)
 * ───────────────────────────────────────────────────────────────────────────*/
#define USB_PRINTER_STS_NOT_ERROR    0x08u  /* bit 3: 1 = no error condition  */
#define USB_PRINTER_STS_SELECT       0x10u  /* bit 4: 1 = printer online      */
#define USB_PRINTER_STS_PAPER_EMPTY  0x20u  /* bit 5: 1 = paper-out           */

/* ─────────────────────────────────────────────────────────────────────────────
 * IPP printer-state enumerations (RFC 2911 §4.4.11)
 * ───────────────────────────────────────────────────────────────────────────*/
#define USB_PRINTER_IPP_IDLE        3
#define USB_PRINTER_IPP_PROCESSING  4
#define USB_PRINTER_IPP_STOPPED     5

/* ─────────────────────────────────────────────────────────────────────────────
 * printer_status_t — live printer status, updated by the status thread.
 *
 * Each boolean/byte field is updated atomically (single-byte store on MIPS32).
 * The 32-bit counter fields (jobs_printed, bytes_sent) may be read slightly
 * stale by concurrent readers — this is acceptable for the display-only use
 * case (web UI, IPP attributes).  The device_id[] string is written once
 * after enumeration and is never re-written while readers may access it.
 * For the print-path (lpr, ipp, raw_tcp) readers only check the boolean
 * fields; they do not read multi-word fields under contention.
 * ───────────────────────────────────────────────────────────────────────────*/
typedef struct {
    cyg_bool_t  connected;      /* USB printer device is present          */
    cyg_bool_t  online;         /* printer is selected/online (bit 4)     */
    cyg_bool_t  paper_empty;    /* paper-out condition (bit 5)            */
    cyg_bool_t  error;          /* error condition (bit 3 inverted)       */
    cyg_bool_t  busy;           /* a print job is currently in progress   */
    uint8_t     raw_status;     /* raw GET_PORT_STATUS byte               */
    uint32_t    jobs_printed;   /* running count of completed jobs        */
    uint32_t    bytes_sent;     /* running total of bytes forwarded       */
    char        device_id[256]; /* IEEE 1284 Device ID string (NUL-term.) */
} printer_status_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Global printer status — shared between the status thread and the network
 * protocol threads.  Declared volatile so that the compiler does not cache
 * field reads in registers across the polling loops in ipp_server.c, lpr.c.
 * ───────────────────────────────────────────────────────────────────────────*/
extern volatile printer_status_t g_printer_status;

/* ─────────────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────────────*/

/*
 * usb_printer_init() — initialise the MT7688 USB host controller and,
 * if a printer is already connected, enumerate it.
 *
 * Must be called once from the print-server main thread after the OS has
 * started.  Returns 0 on success; -1 if the controller fails to initialise.
 * A return of 0 does NOT imply that a printer is connected; check
 * g_printer_status.connected or usb_printer_is_connected().
 */
int usb_printer_init(void);

/*
 * usb_printer_write() — send print data to the printer via Bulk OUT.
 *
 * Blocks until all len bytes have been sent or an error occurs.
 * Returns the number of bytes written (== len on success), or -1 on error.
 * Returns 0 if no printer is connected.
 */
int usb_printer_write(const uint8_t *data, size_t len);

/*
 * usb_printer_read() — read back-channel data from the printer via Bulk IN.
 *
 * This is the key function for bi-directional printing: it allows the printer
 * to return status codes, error strings, or PJL responses to the host.
 *
 * Performs a non-blocking read (timeout_ms = 0) or a timed read.
 * Returns the number of bytes placed in buf, 0 if no data was available
 * within the timeout, or -1 on error.
 */
int usb_printer_read(uint8_t *buf, size_t max_len, uint32_t timeout_ms);

/*
 * usb_printer_get_port_status() — GET_PORT_STATUS class request.
 *
 * Reads the 1-byte IEEE 1284 port status from the printer's control endpoint
 * and stores it in *status_byte.  Use USB_PRINTER_STS_* to decode bits.
 * Returns 0 on success, -1 on error.
 */
int usb_printer_get_port_status(uint8_t *status_byte);

/*
 * usb_printer_get_device_id() — GET_DEVICE_ID class request.
 *
 * Fetches the IEEE 1284 Device ID string (which encodes Manufacturer, Model,
 * and supported PDL command sets) and stores a NUL-terminated copy in buf.
 * Returns 0 on success, -1 on error.
 */
int usb_printer_get_device_id(char *buf, size_t max_len);

/*
 * usb_printer_soft_reset() — SOFT_RESET class request.
 *
 * Sends a class-specific reset to the printer.  Useful to clear a stalled
 * Bulk IN or Bulk OUT endpoint after a paper-jam or error condition.
 * Returns 0 on success, -1 on error.
 */
int usb_printer_soft_reset(void);

/*
 * usb_printer_update_status() — poll the printer and refresh g_printer_status.
 *
 * Issues GET_PORT_STATUS to update online/paper/error flags.  If the device
 * ID has not yet been retrieved (first call after connect), also issues
 * GET_DEVICE_ID.  Should be called periodically (every ~2 seconds) from the
 * status thread.
 */
void usb_printer_update_status(void);

/*
 * usb_printer_is_connected() — returns true if a printer is connected and
 * has been successfully enumerated.
 */
cyg_bool_t usb_printer_is_connected(void);

#endif /* USB_PRINTER_H */
