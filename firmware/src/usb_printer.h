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
 * Printer firmware upload (Cypress EZ-USB):
 *   Some printers (HP LaserJet 1015/1020/1022) require a firmware blob to be
 *   uploaded via the Cypress EZ-USB ANCHOR_LOAD_INTERNAL protocol before they
 *   become functional USB Printer Class devices.  This module implements that
 *   upload protocol.  The upload protocol itself is clean-room
 *   reverse-engineered and documented in open-source projects (foo2zjs, HPLIP,
 *   Linux kernel usb/misc/ezusb.c).  The firmware binary must be obtained
 *   separately from HPLIP or the HP Windows driver and stored here via
 *   usb_fw_store() (typically via the web interface POST /api/upload_printer_fw).
 *
 * References:
 *   - Universal Serial Bus Specification, Revision 2.0
 *   - USB Printer Class Definition for Printing Devices, Release 1.1
 *   - Enhanced Host Controller Interface (EHCI) Specification, Release 1.0
 *   - MT7628AN/MT7688 Datasheet, MediaTek Inc.
 *   - Cypress Semiconductor EZ-USB Technical Reference Manual
 *   - Linux kernel: drivers/usb/misc/ezusb.c (ANCHOR_LOAD_INTERNAL)
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
 *
 * needs_firmware: set when a USB device is present but requires the host to
 * upload firmware before it can function as a printer.  Classic examples are
 * the HP LaserJet 1015/1020/1022 which enumerate with a Cypress EZ-USB stub
 * PID at power-on and only become functional printer-class devices after a
 * host uploads the printer firmware (done automatically by HPLIP or foo2zjs
 * on a connected PC).  When this flag is set, connected=false and print jobs
 * are rejected.  The flag is cleared when the USB device is physically
 * disconnected.
 *
 * State invariant: connected=true implies needs_firmware=false.
 * When needs_firmware=true the USB device is physically present (PORTSC_CCS=1)
 * but has not been successfully enumerated as a Printer Class device.
 * ───────────────────────────────────────────────────────────────────────────*/
typedef struct {
    cyg_bool_t  connected;       /* USB printer is present, enumerated, and
                                  * ready to accept print data.  True only
                                  * when needs_firmware=false and the device
                                  * has been configured as a Printer Class
                                  * device.                                 */
    cyg_bool_t  online;          /* printer is selected/online (bit 4)     */
    cyg_bool_t  paper_empty;     /* paper-out condition (bit 5)            */
    cyg_bool_t  error;           /* error condition (bit 3 inverted)       */
    cyg_bool_t  busy;            /* a print job is currently in progress   */
    cyg_bool_t  needs_firmware;  /* device needs host-side firmware upload */
    uint8_t     raw_status;      /* raw GET_PORT_STATUS byte               */
    uint32_t    jobs_printed;    /* running count of completed jobs        */
    uint32_t    bytes_sent;      /* running total of bytes forwarded       */
    char        device_id[256];  /* IEEE 1284 Device ID string (NUL-term.) */
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

/* ─────────────────────────────────────────────────────────────────────────────
 * Printer firmware blob storage — Cypress EZ-USB ANCHOR_LOAD_INTERNAL
 *
 * The HP LaserJet 1015, 1020, and 1022 (and similar Cypress EZ-USB–based
 * printers) store their operating firmware in RAM.  At every power-on the
 * printer enumerates as a vendor-specific stub device (no Printer Class
 * interface) and a host must upload the firmware before the printer becomes
 * functional.
 *
 * The upload protocol is the Cypress EZ-USB ANCHOR_LOAD_INTERNAL vendor
 * request (bRequest=0xA0, bmRequestType=0x40).  This protocol has been
 * clean-room reverse-engineered by the open-source community and is
 * documented in:
 *   - Linux kernel: drivers/usb/misc/ezusb.c
 *   - foo2zjs: usb/foo2usb-wrapper, firmware/Makefile
 *   - HPLIP: base/firmware.py
 *
 * The firmware binary itself (hp_laserjet_1020.fw from HPLIP, or
 * sihp1020.img from foo2zjs) is HP-proprietary and cannot be redistributed.
 * Users must obtain it from the HPLIP package or the HP Windows driver and
 * upload it to this print server via POST /api/upload_printer_fw.
 *
 * Supported firmware formats:
 *   Intel HEX  — detected by ':' at offset 0 (HPLIP .fw files)
 *   Raw binary — any other content (foo2zjs .img files); loaded at 0x0000
 *
 * When a stub-PID printer is detected and a firmware blob is stored here,
 * usb_printer_update_status() automatically performs the upload sequence:
 *   1. Hold EZ-USB CPU in reset  (ANCHOR_LOAD to 0xE600, data={0x01})
 *   2. Upload firmware chunks    (ANCHOR_LOAD to each target address)
 *   3. Release EZ-USB CPU        (ANCHOR_LOAD to 0xE600, data={0x00})
 *   4. Wait for re-enumeration   (device reconnects with printer PID)
 * ───────────────────────────────────────────────────────────────────────────*/

/* Maximum firmware blob size in bytes (HP LJ 1020 fw is ~50 KB). */
#define USB_FW_MAX_SIZE  (64u * 1024u)

/* Return codes from the internal firmware upload sequence. */
#define USB_FW_OK            0
#define USB_FW_ERR_NO_BLOB  (-1)  /* no firmware blob stored               */
#define USB_FW_ERR_USB      (-2)  /* USB error during ANCHOR_LOAD transfer  */
#define USB_FW_ERR_FORMAT   (-3)  /* Intel HEX parse / checksum error       */
#define USB_FW_ERR_TIMEOUT  (-4)  /* device did not re-enumerate in time    */

/*
 * usb_fw_store() — copy a firmware blob into the internal buffer.
 *
 * data must point to either:
 *   - An Intel HEX file (detected by ':' at byte 0, e.g. HPLIP .fw), or
 *   - A raw binary image (any other first byte, e.g. foo2zjs .img).
 *
 * Returns 0 on success, -1 if data is NULL, len is 0, or len exceeds
 * USB_FW_MAX_SIZE.
 */
int usb_fw_store(const uint8_t *data, size_t len);

/*
 * usb_fw_get_write_buf() — return a pointer to the firmware write buffer.
 *
 * Used by httpd.c to receive firmware data directly into the blob buffer
 * without an intermediate copy.  Sets *max_len to USB_FW_MAX_SIZE.
 *
 * This function atomically clears the stored blob size (under the mutex) so
 * that do_fw_upload() in the status thread cannot begin reading a
 * partially-written blob.  Call usb_fw_commit() with the actual received
 * byte count once writing is complete.
 *
 * Thread safety: safe to call from any thread.  The blob bytes between this
 * call and the corresponding usb_fw_commit() must only be written by the
 * calling thread.
 */
uint8_t *usb_fw_get_write_buf(size_t *max_len);

/*
 * usb_fw_commit() — finalise the firmware blob written via
 * usb_fw_get_write_buf().  len must be > 0 and <= USB_FW_MAX_SIZE.
 * If len is 0 or out of range the call is a no-op.
 */
void usb_fw_commit(size_t len);

/*
 * usb_fw_has_blob() — returns non-zero if a firmware blob is stored.
 */
int usb_fw_has_blob(void);

/*
 * usb_fw_blob_size() — returns the size of the stored firmware blob (0 if
 * none is stored).
 */
size_t usb_fw_blob_size(void);

#endif /* USB_PRINTER_H */
