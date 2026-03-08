/*
 * usb_printer.c — USB Printer Class driver with bi-directional support.
 *
 * Implements USB Printer Class (Class 7, Protocol 2 bidirectional) over the
 * MT7688's on-chip USB 2.0 host controller (EHCI-compatible).
 *
 * Data paths implemented:
 *   Forward  (host → printer): usb_printer_write() → Bulk OUT endpoint
 *   Backward (printer → host): usb_printer_read()  → Bulk IN  endpoint
 *
 * Status back-channel (bi-directional):
 *   usb_printer_get_port_status() — GET_PORT_STATUS control request
 *                                   returns online/paper/error bits
 *   usb_printer_get_device_id()   — GET_DEVICE_ID control request
 *                                   returns IEEE 1284 Device ID string
 *
 * The driver uses a polling transfer model: the EHCI asynchronous schedule
 * is started for each transfer and the qTD Active bit is polled to completion.
 * This is appropriate for a single-threaded embedded print server; no
 * interrupts are required.
 *
 * MT7688 USB host controller:
 *   Physical base address: 0x101C0000
 *   KSEG1 uncached alias:  0xB01C0000
 *   System-control clock gate: SYSCTRL base 0xB0000000, clock reg +0x030
 *
 * References:
 *   - Universal Serial Bus Specification, Revision 2.0
 *   - USB Printer Class Definition for Printing Devices, Release 1.1
 *   - EHCI Specification for USB, Release 1.0
 *   - MT7628AN/MT7688 Datasheet, MediaTek Inc.
 */

#include "usb_printer.h"
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Optional built-in firmware blob (generated at build time from HP1020_FW).
 * Included only when compiled with -DHAVE_HP1020_FW_BUILTIN.             */
#ifdef HAVE_HP1020_FW_BUILTIN
#  include "hp1020_fw_blob.h"
#endif

/* ─────────────────────────────────────────────────────────────────────────────
 * Global printer status (shared with ipp_server, lpr, httpd)
 * ───────────────────────────────────────────────────────────────────────────*/
volatile printer_status_t g_printer_status;

/* ─────────────────────────────────────────────────────────────────────────────
 * MT7688 System Controller — USB clock and mode
 *
 * SYSCTRL_BASE  = physical 0x10000000 → KSEG1 0xB0000000
 * CLKGATECR     = SYSCTRL + 0x030  (Clock Gate Control Register)
 *   bit 18 = USB host clock enable
 * USBMODECR     = SYSCTRL + 0x028  (USB Mode Control)
 *   bit 0  = USB host mode (1 = host, 0 = device)
 * ───────────────────────────────────────────────────────────────────────────*/
#define SYSCTRL_BASE    0xB0000000UL
#define CLKGATECR       (*(volatile uint32_t *)(SYSCTRL_BASE + 0x030))
#define USBMODECR       (*(volatile uint32_t *)(SYSCTRL_BASE + 0x028))
#define CLKGATE_USB     (1u << 18)
#define USBMODE_HOST    (1u << 0)

/* ─────────────────────────────────────────────────────────────────────────────
 * MT7688 EHCI Host Controller registers
 *
 * Physical base: 0x101C0000  →  KSEG1: 0xB01C0000
 * Capability registers start at the base address.
 * Operational registers start at base + CAPLENGTH (typically 0x20).
 * ───────────────────────────────────────────────────────────────────────────*/
#define EHCI_BASE       0xB01C0000UL

/* Capability registers */
#define EHCI_CAPLENGTH  (*(volatile uint8_t  *)(EHCI_BASE + 0x00))
#define EHCI_HCIVERSION (*(volatile uint16_t *)(EHCI_BASE + 0x02))
#define EHCI_HCSPARAMS  (*(volatile uint32_t *)(EHCI_BASE + 0x04))
#define EHCI_HCCPARAMS  (*(volatile uint32_t *)(EHCI_BASE + 0x08))

/* Operational register offsets (from base + CAPLENGTH) */
#define EHCI_OPR_USBCMD          0x00u
#define EHCI_OPR_USBSTS          0x04u
#define EHCI_OPR_USBINTR         0x08u
#define EHCI_OPR_FRINDEX         0x0Cu
#define EHCI_OPR_PERIODICBASE    0x14u
#define EHCI_OPR_ASYNCLISTADDR   0x18u
#define EHCI_OPR_CONFIGFLAG      0x40u
#define EHCI_OPR_PORTSC0         0x44u

/* USBCMD bits */
#define USBCMD_RUN      (1u << 0)   /* Run/Stop                           */
#define USBCMD_HCRESET  (1u << 1)   /* Host Controller Reset              */
#define USBCMD_ASYNC_EN (1u << 5)   /* Asynchronous Schedule Enable       */
#define USBCMD_ASYNC_DB (1u << 6)   /* Interrupt on Async Advance Doorbell*/

/* USBSTS bits */
#define USBSTS_ASYNC_ADV (1u << 5)  /* Interrupt on Async Advance         */
#define USBSTS_HCHALTED  (1u << 12) /* HC Halted                          */

/* PORTSC bits */
#define PORTSC_CCS      (1u << 0)   /* Current Connect Status             */
#define PORTSC_CSC      (1u << 1)   /* Connect Status Change              */
#define PORTSC_PE       (1u << 2)   /* Port Enable/Disable                */
#define PORTSC_PEC      (1u << 3)   /* Port Enable Change                 */
#define PORTSC_PR       (1u << 8)   /* Port Reset                         */
#define PORTSC_LS_MASK  (3u << 10)  /* Line Status                        */
#define PORTSC_PP       (1u << 12)  /* Port Power                         */
#define PORTSC_SPEED_MASK (3u << 26) /* Port Speed (if supported)         */

/* qTD Token bits */
#define QTD_TOKEN_ACTIVE  (1u << 7)   /* transaction active                */
#define QTD_TOKEN_HALT    (1u << 6)   /* stall / halt                      */
#define QTD_TOKEN_BUFERR  (1u << 5)   /* data buffer error                 */
#define QTD_TOKEN_BABBLE  (1u << 4)   /* babble detected                   */
#define QTD_TOKEN_XACTERR (1u << 3)   /* transaction error                 */
#define QTD_TOKEN_MISSED  (1u << 2)   /* missed micro-frame                */
#define QTD_TOKEN_ERRORS  (QTD_TOKEN_HALT|QTD_TOKEN_BUFERR| \
                           QTD_TOKEN_BABBLE|QTD_TOKEN_XACTERR)
#define QTD_TOKEN_PID_OUT (0u << 8)
#define QTD_TOKEN_PID_IN  (1u << 8)
#define QTD_TOKEN_PID_SETUP (2u << 8)
#define QTD_TOKEN_IOC     (1u << 15)  /* interrupt on complete             */
#define QTD_TOKEN_BYTES(n) ((uint32_t)(n) << 16)
#define QTD_TOKEN_DT(n)   ((uint32_t)(n) << 31)
#define QTD_TOKEN_CERR(n) ((uint32_t)(n) << 10)

/* QH Horizontal Link Pointer: T-bit (terminate), type=QH */
#define QH_TYPE_QH      (1u << 1)
#define QH_TERMINATE    (1u << 0)

/* qTD pointer alignment mask — lower 5 bits are flags, not address bits */
#define QTD_PTR_MASK    (~0x1Fu)

/* EHCI bulk-transfer limits (EHCI spec §4.10.6) */
#define EHCI_PAGE_SIZE       4096u
#define EHCI_MAX_QTD_PAGES   5u
#define EHCI_MAX_QTD_BYTES   (EHCI_MAX_QTD_PAGES * EHCI_PAGE_SIZE) /* 20 kB */

/* QH Endpoint Characteristics word */
#define QH_CHR_DEVADDR(a) ((uint32_t)(a))
#define QH_CHR_EPN(n)     ((uint32_t)(n) << 8)
#define QH_CHR_EPS_FS     (0u << 12) /* Full Speed  */
#define QH_CHR_EPS_LS     (1u << 12) /* Low Speed   */
#define QH_CHR_EPS_HS     (2u << 12) /* High Speed  */
#define QH_CHR_DTC        (1u << 14) /* Data Toggle Control (from qTD)    */
#define QH_CHR_MAXPKT(n)  ((uint32_t)(n) << 16)
#define QH_CHR_CTRL_EP    (1u << 27) /* Control Endpoint (LS/FS on hub)   */

/* QH Endpoint Capabilities word */
#define QH_CAP_MULT(n)    ((uint32_t)(n) << 30)

/* VIRT → PHYS: strip MIPS segment bits to get physical address for EHCI DMA */
#define VIRT_TO_PHYS(v)   ((uint32_t)(uintptr_t)(v) & 0x1FFFFFFFu)

/* ─────────────────────────────────────────────────────────────────────────────
 * EHCI data structures
 *
 * Both structures must be 32-byte aligned (EHCI requirement).
 * We place them in a KSEG1 (uncached) region so that EHCI DMA reads/writes
 * are visible without cache-flush operations.  Applying the attribute
 * __attribute__((section(".nocache"))) would require a linker-script change;
 * instead we use an aligned static array in normal KSEG0 memory and rely on
 * cache synchronisation after each DMA-visible write (see cache_sync() below).
 * ───────────────────────────────────────────────────────────────────────────*/
typedef struct {
    uint32_t  qhlp;         /* Horizontal link pointer (next QH or T-bit) */
    uint32_t  chr;          /* Endpoint characteristics                   */
    uint32_t  cap;          /* Endpoint capabilities                      */
    uint32_t  cur_qtd;      /* Current qTD pointer                        */
    /* Transfer overlay — mirrors the qTD layout */
    uint32_t  next_qtd;
    uint32_t  alt_qtd;
    uint32_t  token;
    uint32_t  buf[5];
    /* Padding to 64 bytes so that two adjacent QHs do not share a 32-byte
     * cache line, avoiding false sharing during polling. */
    uint32_t  _pad[4];
} __attribute__((aligned(32))) ehci_qh_t;

typedef struct {
    uint32_t  next;         /* Next qTD pointer (or T-bit)                */
    uint32_t  alt_next;     /* Alternate next qTD pointer                 */
    uint32_t  token;        /* Status / control token                     */
    uint32_t  buf[5];       /* Buffer page pointers (5 × 4 kB pages)      */
} __attribute__((aligned(32))) ehci_qtd_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Driver state
 * ───────────────────────────────────────────────────────────────────────────*/
#define USB_MAX_PACKET_EP0     64u
#define USB_MAX_PACKET_BULK    512u
#define USB_PRINTER_IFACE      0u   /* printer interface index             */

/* USB addresses and endpoint numbers discovered during enumeration */
static uint8_t  s_dev_addr;         /* assigned USB device address (1–127) */
static uint8_t  s_ep_bulk_out;      /* bulk OUT endpoint number            */
static uint8_t  s_ep_bulk_in;       /* bulk IN  endpoint number            */
static uint16_t s_max_pkt_bulk_out; /* bulk OUT max packet size            */
static uint16_t s_max_pkt_bulk_in;  /* bulk IN  max packet size            */
static uint8_t  s_dev_speed;        /* 0=FS, 2=HS                          */
static uint8_t  s_config_value;     /* bConfigurationValue to SET_CONFIG   */

/* Data-toggle bits for bulk endpoints */
static uint8_t  s_dt_out;          /* current data toggle for bulk OUT    */
static uint8_t  s_dt_in;           /* current data toggle for bulk IN     */

/* EHCI operational register base (set at runtime after reading CAPLENGTH) */
static volatile uint32_t *s_opr;   /* pointer to first operational reg    */

/* DMA-accessible EHCI structures (32-byte aligned by __attribute__) */
static ehci_qh_t   s_qh_ctrl;      /* Queue Head for EP0 control           */
static ehci_qh_t   s_qh_bulk;      /* Queue Head for bulk IN/OUT           */
static ehci_qtd_t  s_qtd[4];       /* Transfer Descriptors (pool of 4)     */

/* Working buffer for USB setup packets and small control transfers */
static uint8_t  s_ctrl_buf[256] __attribute__((aligned(4)));

/* Flag: has the driver been initialised? */
static cyg_bool_t s_initialised = false;

/* ─────────────────────────────────────────────────────────────────────────────
 * Printer firmware blob — Cypress EZ-USB ANCHOR_LOAD_INTERNAL
 *
 * Up to USB_FW_MAX_SIZE bytes of firmware data for stub-PID printers.
 * Protected by s_fw_mutex; written by httpd (HTTP thread) via usb_fw_store()
 * or usb_fw_commit(), read by the status thread during auto-upload.
 * ───────────────────────────────────────────────────────────────────────────*/
static uint8_t     s_fw_blob[USB_FW_MAX_SIZE] __attribute__((aligned(4)));
static size_t      s_fw_blob_size;
static cyg_mutex_t s_fw_mutex;

/* ─────────────────────────────────────────────────────────────────────────────
 * Cache synchronisation
 *
 * On MIPS32, KSEG0 memory is cached.  After writing an EHCI descriptor that
 * the DMA engine will read, we must flush the write-back cache lines to
 * main memory.  After the DMA engine has written to a descriptor, we must
 * invalidate the cache before reading.
 *
 * The MIPS32r2 'synci' instruction performs cache-line writeback+invalidate
 * for instruction caches.  For data caches we use the 'cache' instruction.
 * The 'sync' instruction serialises memory accesses.
 * ───────────────────────────────────────────────────────────────────────────*/
static inline void cache_writeback(const void *addr, size_t len)
{
    const uint8_t *p = (const uint8_t *)addr;
    const uint8_t *end = p + len;
    while (p < end) {
        __asm__ volatile(
            "cache  25, 0(%0)\n"   /* Hit_Writeback_D — writeback D-cache  */
            "sync\n"
            :: "r"(p) : "memory");
        p += 32;
    }
}

static inline void cache_invalidate(const void *addr, size_t len)
{
    const uint8_t *p = (const uint8_t *)addr;
    const uint8_t *end = p + len;
    while (p < end) {
        __asm__ volatile(
            "cache  17, 0(%0)\n"   /* Hit_Invalidate_D — invalidate D-cache*/
            "sync\n"
            :: "r"(p) : "memory");
        p += 32;
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * EHCI operational register helpers
 * ───────────────────────────────────────────────────────────────────────────*/
static inline uint32_t ehci_read(uint32_t offset)
{
    return s_opr[offset / 4];
}

static inline void ehci_write(uint32_t offset, uint32_t val)
{
    s_opr[offset / 4] = val;
    __asm__ volatile("sync" ::: "memory");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Busy-wait helpers
 * ───────────────────────────────────────────────────────────────────────────*/
static void udelay(uint32_t us)
{
    /* Approximate busy-wait: ~50 cycles/µs calibrated for MT7688 at 575 MHz.
     * Accuracy is not critical — this is only used for post-SET_ADDRESS
     * stabilisation (2 ms).  If the CPU clock differs, adjust the multiplier.
     * Production code should use a hardware timer for accurate delays. */
    volatile uint32_t i;
    for (i = 0; i < us * 50u; i++)
        ;
}

static void mdelay(uint32_t ms)
{
    cyg_thread_delay(pdMS_TO_TICKS(ms) + 1u);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * MT7688 platform USB initialisation
 *
 * 1. Enable the USB host clock via the System Controller clock-gate register.
 * 2. Select USB host mode in the USB mode-control register.
 * 3. Allow the PHY to settle.
 * ───────────────────────────────────────────────────────────────────────────*/
static void platform_usb_init(void)
{
    /* Enable USB host clock (bit 18 of clock-gate register) */
    CLKGATECR |= CLKGATE_USB;
    __asm__ volatile("sync" ::: "memory");

    /* Select USB host mode (bit 0 of USB mode-control register) */
    USBMODECR |= USBMODE_HOST;
    __asm__ volatile("sync" ::: "memory");

    /* Allow PHY to power up and stabilise */
    mdelay(50);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * EHCI controller initialisation
 *
 * Follows the EHCI spec §4.1 initialisation sequence:
 *   1. Reset the host controller.
 *   2. Set up operational registers (interrupt mask, frame list base, etc.).
 *   3. Start the host controller (Run bit).
 *   4. Set CONFIGFLAG=1 to route all ports to EHCI.
 * ───────────────────────────────────────────────────────────────────────────*/
static int ehci_init(void)
{
    uint8_t  caplength;
    uint32_t n;

    /* Determine operational register base from CAPLENGTH */
    caplength = EHCI_CAPLENGTH;
    if (caplength < 0x10u || caplength > 0x80u) {
        diag_printf("usb: CAPLENGTH=0x%02x invalid\n", (unsigned)caplength);
        return -1;
    }
    s_opr = (volatile uint32_t *)(EHCI_BASE + caplength);

    /* Reset the host controller */
    ehci_write(EHCI_OPR_USBCMD,
               ehci_read(EHCI_OPR_USBCMD) | USBCMD_HCRESET);

    for (n = 0; n < 100u; n++) {
        mdelay(1);
        if (!(ehci_read(EHCI_OPR_USBCMD) & USBCMD_HCRESET))
            break;
    }
    if (ehci_read(EHCI_OPR_USBCMD) & USBCMD_HCRESET) {
        diag_printf("usb: EHCI reset timeout\n");
        return -1;
    }

    /* Disable all interrupts (we poll) */
    ehci_write(EHCI_OPR_USBINTR, 0);

    /* No periodic schedule; we only use the asynchronous schedule */
    ehci_write(EHCI_OPR_PERIODICBASE, 0);

    /* Set up a dummy async list head — a self-linking QH with H=1 */
    memset(&s_qh_ctrl, 0, sizeof(s_qh_ctrl));
    s_qh_ctrl.qhlp = VIRT_TO_PHYS(&s_qh_ctrl) | QH_TYPE_QH;
    s_qh_ctrl.chr  = QH_CHR_EPS_HS | QH_CHR_DTC | (1u << 15) /* H-bit */;
    s_qh_ctrl.cap  = QH_CAP_MULT(1);
    s_qh_ctrl.next_qtd = QH_TERMINATE;
    s_qh_ctrl.alt_qtd  = QH_TERMINATE;
    cache_writeback(&s_qh_ctrl, sizeof(s_qh_ctrl));

    ehci_write(EHCI_OPR_ASYNCLISTADDR, VIRT_TO_PHYS(&s_qh_ctrl));

    /* Start the host controller */
    ehci_write(EHCI_OPR_USBCMD,
               USBCMD_RUN | (8u << 16) /* frame list size 256 entries */);

    for (n = 0; n < 20u; n++) {
        mdelay(1);
        if (!(ehci_read(EHCI_OPR_USBSTS) & USBSTS_HCHALTED))
            break;
    }
    if (ehci_read(EHCI_OPR_USBSTS) & USBSTS_HCHALTED) {
        diag_printf("usb: EHCI did not start\n");
        return -1;
    }

    /* Route all ports to EHCI (not companion OHCI) */
    ehci_write(EHCI_OPR_CONFIGFLAG, 1);
    mdelay(5);

    diag_printf("usb: EHCI controller started (HCIVERSION=0x%04x)\n",
                (unsigned)EHCI_HCIVERSION);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Port reset and speed detection
 *
 * Issues a USB bus reset on port 0 and waits for the port to be enabled
 * by the EHCI controller.  After reset, reads the port speed.
 * Returns 0 on success (device connected and port enabled), -1 otherwise.
 * ───────────────────────────────────────────────────────────────────────────*/
static int ehci_port_reset(void)
{
    uint32_t portsc;
    uint32_t n;

    /* Enable port power */
    portsc = ehci_read(EHCI_OPR_PORTSC0);
    if (!(portsc & PORTSC_PP)) {
        ehci_write(EHCI_OPR_PORTSC0, portsc | PORTSC_PP);
        mdelay(20);
    }

    /* Check for device presence */
    portsc = ehci_read(EHCI_OPR_PORTSC0);
    if (!(portsc & PORTSC_CCS)) {
        return -1;   /* nothing connected */
    }

    /* Assert reset for 50 ms (USB spec §7.1.7.5) */
    ehci_write(EHCI_OPR_PORTSC0,
               (portsc & ~PORTSC_PE) | PORTSC_PR);
    mdelay(50);

    /* Deassert reset */
    portsc = ehci_read(EHCI_OPR_PORTSC0);
    ehci_write(EHCI_OPR_PORTSC0, portsc & ~PORTSC_PR);

    /* Wait for the port to be enabled (EHCI does this automatically for
     * high-speed devices; full-speed devices are released to OHCI). */
    for (n = 0; n < 100u; n++) {
        mdelay(1);
        portsc = ehci_read(EHCI_OPR_PORTSC0);
        if (portsc & PORTSC_PE)
            break;
    }

    if (!(portsc & PORTSC_PE)) {
        /* Port not enabled — full-speed device or no device */
        diag_printf("usb: port not enabled after reset (portsc=0x%08x)\n",
                    (unsigned)portsc);
        return -1;
    }

    /* Read line-status to determine speed */
    s_dev_speed = (uint8_t)((portsc >> 26) & 0x3u);
    diag_printf("usb: port reset OK — connected, speed=%u\n",
                (unsigned)s_dev_speed);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * EHCI transfer engine — execute a single USB transaction
 *
 * Sets up the async schedule QH for the target endpoint and chains a list
 * of qTDs, then polls for completion.  Returns 0 on success, -1 on error.
 *
 * Parameters:
 *   is_ctrl    — true for control transfers on EP0 (sets H=0, DTC=1)
 *   ep_num     — endpoint number
 *   max_pkt    — maximum packet size for the endpoint
 *   qtd_head   — pointer to the first qTD in the chain
 *   timeout_ms — time to wait for completion in milliseconds
 * ───────────────────────────────────────────────────────────────────────────*/
static int ehci_run_transfer(cyg_bool_t is_ctrl, uint8_t ep_num,
                              uint16_t max_pkt, ehci_qtd_t *qtd_head,
                              uint32_t timeout_ms)
{
    ehci_qh_t *qh = &s_qh_bulk;
    uint32_t   chr, cap;
    uint32_t   elapsed = 0;

    /* Configure the Queue Head for this endpoint */
    chr = QH_CHR_DEVADDR(s_dev_addr)
        | QH_CHR_EPN(ep_num)
        | QH_CHR_MAXPKT(max_pkt)
        | QH_CHR_DTC
        | (1u << 15); /* H-bit: required on the sole QH in the async list */
    if (s_dev_speed == 2u) {
        chr |= QH_CHR_EPS_HS;
    } else {
        chr |= QH_CHR_EPS_FS;
        if (is_ctrl)
            chr |= QH_CHR_CTRL_EP;
    }
    if (is_ctrl) {
        qh = &s_qh_ctrl;
    }

    cap = QH_CAP_MULT(1);

    /* Link the QH to itself (it is the only element in the list) */
    memset(qh, 0, sizeof(*qh));
    qh->qhlp    = VIRT_TO_PHYS(qh) | QH_TYPE_QH;
    qh->chr     = chr;
    qh->cap     = cap;
    qh->cur_qtd = 0;
    qh->next_qtd = VIRT_TO_PHYS(qtd_head);
    qh->alt_qtd  = QH_TERMINATE;
    qh->token    = 0;
    cache_writeback(qh, sizeof(*qh));

    /* Point the async schedule at our QH */
    ehci_write(EHCI_OPR_ASYNCLISTADDR, VIRT_TO_PHYS(qh));

    /* Enable the async schedule */
    ehci_write(EHCI_OPR_USBCMD,
               ehci_read(EHCI_OPR_USBCMD) | USBCMD_ASYNC_EN);

    /* Poll until the last qTD's Active bit clears */
    for (;;) {
        ehci_qtd_t *qtd = qtd_head;
        uint32_t    token;

        /* Walk to the last qTD in the chain */
        while (!(qtd->next & QH_TERMINATE)) {
            cache_invalidate(qtd, sizeof(*qtd));
            qtd = (ehci_qtd_t *)(uintptr_t)(qtd->next & QTD_PTR_MASK);
        }
        cache_invalidate(qtd, sizeof(*qtd));
        token = qtd->token;

        if (!(token & QTD_TOKEN_ACTIVE)) {
            /* Transfer complete — check for errors */
            if (token & QTD_TOKEN_ERRORS) {
                diag_printf("usb: transfer error (token=0x%08x)\n",
                            (unsigned)token);
                ehci_write(EHCI_OPR_USBCMD,
                           ehci_read(EHCI_OPR_USBCMD) & ~USBCMD_ASYNC_EN);
                return -1;
            }
            break;
        }

        if (elapsed >= timeout_ms) {
            diag_printf("usb: transfer timeout\n");
            ehci_write(EHCI_OPR_USBCMD,
                       ehci_read(EHCI_OPR_USBCMD) & ~USBCMD_ASYNC_EN);
            return -1;
        }

        cyg_thread_delay(pdMS_TO_TICKS(1));
        elapsed++;
    }

    /* Disable the async schedule */
    ehci_write(EHCI_OPR_USBCMD,
               ehci_read(EHCI_OPR_USBCMD) & ~USBCMD_ASYNC_EN);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * USB control transfer
 *
 * Performs a standard three-phase control transfer (SETUP / DATA / STATUS)
 * on EP0.  dir_in=true for IN transfers (device→host), false for OUT.
 *
 * setup_pkt — 8-byte USB setup packet
 * data      — payload buffer (NULL for zero-length transfers)
 * len       — payload length in bytes
 * ───────────────────────────────────────────────────────────────────────────*/
static int usb_control_transfer(const uint8_t *setup_pkt,
                                 void *data, uint16_t len,
                                 cyg_bool_t dir_in)
{
    /* We use up to 3 qTDs: SETUP, DATA (optional), STATUS */
    ehci_qtd_t *qtd_setup  = &s_qtd[0];
    ehci_qtd_t *qtd_data   = &s_qtd[1];
    ehci_qtd_t *qtd_status = &s_qtd[2];

    /* ── SETUP phase ── */
    memcpy(s_ctrl_buf, setup_pkt, 8);
    cache_writeback(s_ctrl_buf, 8);

    memset(qtd_setup, 0, sizeof(*qtd_setup));
    qtd_setup->token   = QTD_TOKEN_PID_SETUP
                       | QTD_TOKEN_BYTES(8)
                       | QTD_TOKEN_CERR(3)
                       | QTD_TOKEN_ACTIVE;
    qtd_setup->buf[0]  = VIRT_TO_PHYS(s_ctrl_buf);

    if (len > 0) {
        /* ── DATA phase ── */
        if (dir_in && data) {
            cache_invalidate(data, len);
        } else if (data) {
            cache_writeback(data, len);
        }
        memset(qtd_data, 0, sizeof(*qtd_data));
        qtd_data->token   = (dir_in ? QTD_TOKEN_PID_IN : QTD_TOKEN_PID_OUT)
                          | QTD_TOKEN_DT(1)
                          | QTD_TOKEN_BYTES(len)
                          | QTD_TOKEN_CERR(3)
                          | QTD_TOKEN_ACTIVE;
        qtd_data->buf[0]  = VIRT_TO_PHYS(data);
        if (len > EHCI_PAGE_SIZE - (VIRT_TO_PHYS(data) & (EHCI_PAGE_SIZE - 1u))) {
            qtd_data->buf[1] = (VIRT_TO_PHYS(data) & ~(EHCI_PAGE_SIZE - 1u)) + EHCI_PAGE_SIZE;
        }
        qtd_setup->next   = VIRT_TO_PHYS(qtd_data);

        /* ── STATUS phase ── */
        memset(qtd_status, 0, sizeof(*qtd_status));
        qtd_status->token  = (dir_in ? QTD_TOKEN_PID_OUT : QTD_TOKEN_PID_IN)
                           | QTD_TOKEN_DT(1)
                           | QTD_TOKEN_IOC
                           | QTD_TOKEN_CERR(3)
                           | QTD_TOKEN_ACTIVE;
        qtd_status->next   = QH_TERMINATE;
        qtd_status->alt_next = QH_TERMINATE;
        qtd_data->next     = VIRT_TO_PHYS(qtd_status);
        qtd_data->alt_next = QH_TERMINATE;
    } else {
        /* Zero-length transfer: SETUP + STATUS only */
        memset(qtd_status, 0, sizeof(*qtd_status));
        qtd_status->token  = (dir_in ? QTD_TOKEN_PID_OUT : QTD_TOKEN_PID_IN)
                           | QTD_TOKEN_DT(1)
                           | QTD_TOKEN_IOC
                           | QTD_TOKEN_CERR(3)
                           | QTD_TOKEN_ACTIVE;
        qtd_status->next   = QH_TERMINATE;
        qtd_status->alt_next = QH_TERMINATE;
        qtd_setup->next    = VIRT_TO_PHYS(qtd_status);
    }

    qtd_setup->alt_next = QH_TERMINATE;

    cache_writeback(qtd_setup,  sizeof(*qtd_setup));
    cache_writeback(qtd_data,   sizeof(*qtd_data));
    cache_writeback(qtd_status, sizeof(*qtd_status));

    return ehci_run_transfer(true, 0, USB_MAX_PACKET_EP0,
                             qtd_setup, 2000u);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * USB Bulk transfer (OUT or IN)
 *
 * Sends (dir_in=false) or receives (dir_in=true) up to len bytes on the
 * bulk endpoint.  Automatically handles data-toggle tracking.
 * Returns bytes transferred on success, -1 on error.
 * ───────────────────────────────────────────────────────────────────────────*/
static int usb_bulk_transfer(cyg_bool_t dir_in, void *buf, size_t len,
                              uint32_t timeout_ms)
{
    uint8_t     ep_num  = dir_in ? s_ep_bulk_in  : s_ep_bulk_out;
    uint16_t    max_pkt = dir_in ? s_max_pkt_bulk_in : s_max_pkt_bulk_out;
    uint8_t    *dt      = dir_in ? &s_dt_in : &s_dt_out;
    ehci_qtd_t *qtd     = &s_qtd[0];
    size_t      xfered  = 0;
    uint8_t    *p       = (uint8_t *)buf;

    if (len == 0) return 0;

    /* For large transfers we send in chunks that fit in one qTD (up to 5
     * 4 kB pages = 20 kB), though in practice print data arrives in 4 kB
     * chunks from the network buffer. */
    while (xfered < len) {
        size_t chunk = len - xfered;
        uint32_t initial_bytes_left;

        if (chunk > EHCI_MAX_QTD_BYTES)
            chunk = EHCI_MAX_QTD_BYTES;

        if (dir_in) {
            cache_invalidate(p + xfered, chunk);
        } else {
            cache_writeback(p + xfered, chunk);
        }

        memset(qtd, 0, sizeof(*qtd));
        qtd->next     = QH_TERMINATE;
        qtd->alt_next = QH_TERMINATE;
        qtd->token    = (dir_in ? QTD_TOKEN_PID_IN : QTD_TOKEN_PID_OUT)
                      | QTD_TOKEN_DT(*dt)
                      | QTD_TOKEN_BYTES(chunk)
                      | QTD_TOKEN_IOC
                      | QTD_TOKEN_CERR(3)
                      | QTD_TOKEN_ACTIVE;
        qtd->buf[0]   = VIRT_TO_PHYS(p + xfered);
        qtd->buf[1]   = (qtd->buf[0] & ~(EHCI_PAGE_SIZE - 1u)) + EHCI_PAGE_SIZE;
        qtd->buf[2]   = qtd->buf[1] + EHCI_PAGE_SIZE;
        qtd->buf[3]   = qtd->buf[2] + EHCI_PAGE_SIZE;
        qtd->buf[4]   = qtd->buf[3] + EHCI_PAGE_SIZE;

        cache_writeback(qtd, sizeof(*qtd));
        initial_bytes_left = chunk;

        if (ehci_run_transfer(false, ep_num, max_pkt, qtd, timeout_ms) < 0) {
            return -1;
        }

        cache_invalidate(qtd, sizeof(*qtd));
        {
            uint32_t bytes_left = (qtd->token >> 16) & 0x7FFFu;
            size_t   done = initial_bytes_left - bytes_left;
            xfered += done;

            /* Toggle data-toggle bit */
            *dt ^= 1u;

            /* Short packet from an IN endpoint means the printer has sent all
             * available data — stop reading without waiting for more. */
            if (dir_in && bytes_left > 0)
                break;
        }
    }
    return (int)xfered;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * USB standard device requests
 * ───────────────────────────────────────────────────────────────────────────*/

/* GET_DESCRIPTOR (bDescriptorType, wIndex, wLength) */
static int usb_get_descriptor(uint8_t type, uint8_t index,
                               uint16_t lang, void *buf, uint16_t len)
{
    uint8_t setup[8] = {
        0x80,                /* bmRequestType: IN, Standard, Device */
        0x06,                /* bRequest: GET_DESCRIPTOR             */
        index,               /* wValue low:  descriptor index        */
        type,                /* wValue high: descriptor type         */
        (uint8_t)(lang),     /* wIndex low                           */
        (uint8_t)(lang >> 8),/* wIndex high                          */
        (uint8_t)(len),      /* wLength low                          */
        (uint8_t)(len >> 8), /* wLength high                         */
    };
    return usb_control_transfer(setup, buf, len, true);
}

/* SET_ADDRESS */
static int usb_set_address(uint8_t addr)
{
    uint8_t setup[8] = {
        0x00,   /* bmRequestType: OUT, Standard, Device */
        0x05,   /* bRequest: SET_ADDRESS                */
        addr, 0,/* wValue: new address                  */
        0, 0,   /* wIndex                               */
        0, 0,   /* wLength                              */
    };
    return usb_control_transfer(setup, NULL, 0, false);
}

/* SET_CONFIGURATION */
static int usb_set_configuration(uint8_t cfg)
{
    uint8_t setup[8] = {
        0x00,   /* bmRequestType: OUT, Standard, Device */
        0x09,   /* bRequest: SET_CONFIGURATION          */
        cfg, 0, /* wValue: configuration value          */
        0, 0,   /* wIndex                               */
        0, 0,   /* wLength                              */
    };
    return usb_control_transfer(setup, NULL, 0, false);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Cypress EZ-USB firmware upload — ANCHOR_LOAD_INTERNAL protocol
 *
 * The HP LaserJet 1015/1020/1022 use a Cypress EZ-USB (CY7C64xxx) USB
 * controller.  At power-on it enumerates as a vendor-specific stub device;
 * a host must upload operating firmware via the ANCHOR_LOAD_INTERNAL vendor
 * control request before the printer becomes functional.
 *
 * Protocol (clean-room reverse-engineered; see Linux kernel ezusb.c):
 *   bmRequestType = 0x40  (OUT | VENDOR | DEVICE)
 *   bRequest      = 0xA0  (ANCHOR_LOAD_INTERNAL / FW_LOAD)
 *   wValue        = target address in EZ-USB internal RAM (0x0000–0x1FFF)
 *   wIndex        = 0
 *   wLength       = byte count
 *   Data          = firmware bytes to write at wValue
 *
 * Special addresses:
 *   0xE600 = CPUCS (CPU Control/Status register)
 *     write 0x01 → hold 8051 in reset before loading firmware
 *     write 0x00 → release 8051 (firmware begins executing)
 *
 * Firmware file formats supported:
 *   Intel HEX — detected by ':' at byte 0 (HPLIP .fw files).  Each record
 *               specifies its own load address and byte count.
 *   Raw binary — any other first byte (foo2zjs .img files); the entire blob
 *               is written consecutively starting at address 0x0000.
 * ───────────────────────────────────────────────────────────────────────────*/

#define ANCHOR_LOAD_BREQUEST  0xA0u  /* Cypress EZ-USB firmware-load request */
#define CPUCS_ADDR            0xE600u /* EZ-USB CPU Control/Status register   */
#define ANCHOR_MAX_CHUNK      64u    /* max bytes per ANCHOR_LOAD_INTERNAL    */
#define IHEX_MAX_RECORD_DATA  64u    /* max data bytes per Intel HEX record   */

/* Intel HEX record types */
#define IHEX_TYPE_DATA    0x00u
#define IHEX_TYPE_EOF     0x01u
#define IHEX_TYPE_EXT_SEG 0x02u  /* extended segment address (ignored here) */
#define IHEX_TYPE_EXT_LIN 0x04u  /* extended linear address  (ignored here) */

/*
 * anchor_load_internal — send one ANCHOR_LOAD_INTERNAL vendor request.
 * Writes 'len' bytes from 'data' to EZ-USB internal RAM at 'addr'.
 */
static int anchor_load_internal(uint16_t addr, const uint8_t *data,
                                 uint16_t len)
{
    uint8_t setup[8] = {
        0x40u,                   /* bmRequestType: OUT, Vendor, Device      */
        ANCHOR_LOAD_BREQUEST,    /* bRequest: ANCHOR_LOAD_INTERNAL (0xA0)   */
        (uint8_t)(addr),         /* wValue low  — target address low byte   */
        (uint8_t)(addr >> 8u),   /* wValue high — target address high byte  */
        0x00u, 0x00u,            /* wIndex = 0                              */
        (uint8_t)(len),          /* wLength low                             */
        (uint8_t)(len >> 8u),    /* wLength high                            */
    };
    return usb_control_transfer(setup, (void *)data, len, false);
}

/*
 * ihex_nibble / ihex_byte — minimal Intel HEX character parsers.
 */
static int ihex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int ihex_byte(const char *p, uint8_t *out)
{
    int hi = ihex_nibble(p[0]);
    int lo = ihex_nibble(p[1]);
    if (hi < 0 || lo < 0) return -1;
    *out = (uint8_t)((hi << 4) | lo);
    return 0;
}

/*
 * ihex_upload — parse an Intel HEX stream and upload each data record.
 *
 * Processes ':LL AAAA TT [DD...] CC' records.  Data records (type 0x00)
 * are uploaded via anchor_load_internal; EOF (0x01) terminates the loop.
 * Extended-address record types are accepted but the address extension is
 * ignored — the Cypress EZ-USB 8051 has only 8 KB internal RAM and all
 * records fit in a 16-bit address space.
 */
static int ihex_upload(const uint8_t *blob, size_t blob_len)
{
    const char *p   = (const char *)blob;
    const char *end = p + blob_len;

    while (p < end) {
        /* Skip whitespace between records */
        while (p < end && (*p == '\r' || *p == '\n' ||
                           *p == ' '  || *p == '\t'))
            p++;
        if (p >= end) break;

        if (*p != ':') {
            diag_printf("usb_fw: HEX parse error: expected ':'\n");
            return -1;
        }
        p++;  /* consume ':' */

        /* Need at least LLAAAATT (8 hex chars) + checksum (2) */
        if (p + 10 > end) return -1;

        uint8_t blen, ah, al, rtype;
        if (ihex_byte(p,   &blen)  < 0 ||
            ihex_byte(p+2, &ah)    < 0 ||
            ihex_byte(p+4, &al)    < 0 ||
            ihex_byte(p+6, &rtype) < 0)
            return -1;
        p += 8;

        if (blen > IHEX_MAX_RECORD_DATA) return -1;
        /* Need blen*2 hex chars for data + 2 chars for checksum */
        if ((size_t)(end - p) < (size_t)blen * 2u + 2u) return -1;

        /* Read record data bytes */
        uint8_t rdata[IHEX_MAX_RECORD_DATA] __attribute__((aligned(4)));
        uint8_t csum = (uint8_t)(blen + ah + al + rtype);
        uint8_t i;
        for (i = 0; i < blen; i++) {
            if (ihex_byte(p, &rdata[i]) < 0) return -1;
            csum = (uint8_t)(csum + rdata[i]);
            p += 2;
        }

        /* Verify checksum (two's-complement; sum of all bytes = 0 mod 256) */
        uint8_t rec_csum;
        if (ihex_byte(p, &rec_csum) < 0) return -1;
        p += 2;
        if ((uint8_t)(csum + rec_csum) != 0u) {
            diag_printf("usb_fw: HEX checksum error\n");
            return -1;
        }

        if (rtype == IHEX_TYPE_DATA) {
            uint16_t addr = ((uint16_t)ah << 8u) | al;
            if (anchor_load_internal(addr, rdata, blen) < 0) {
                diag_printf("usb_fw: ANCHOR_LOAD failed at 0x%04x\n",
                            (unsigned)addr);
                return -1;
            }
        } else if (rtype == IHEX_TYPE_EOF) {
            break;
        }
        /* IHEX_TYPE_EXT_SEG / IHEX_TYPE_EXT_LIN: accepted, address extension
         * ignored — all Cypress EZ-USB internal RAM fits in 16-bit space. */
    }
    return 0;
}

/*
 * raw_upload — upload a raw binary image starting at EZ-USB address 0x0000.
 * Used for foo2zjs-style .img files which are plain binary blobs.
 */
static int raw_upload(const uint8_t *blob, size_t len)
{
    uint16_t addr   = 0;
    size_t   offset = 0;

    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > ANCHOR_MAX_CHUNK) chunk = ANCHOR_MAX_CHUNK;

        if (anchor_load_internal(addr, blob + offset, (uint16_t)chunk) < 0) {
            diag_printf("usb_fw: raw upload failed at addr 0x%04x\n",
                        (unsigned)addr);
            return -1;
        }
        offset += chunk;
        addr    = (uint16_t)(addr + (uint16_t)chunk);
    }
    return 0;
}

/*
 * wait_for_reenumeration — after releasing EZ-USB CPU from reset, wait for
 * the device to disconnect and reconnect on the USB bus.
 *
 * After reset release the EZ-USB firmware boots, detaches from the bus, and
 * re-attaches with the printer's operational PID.  At the EHCI level this
 * appears as PORTSC_CCS toggling 0→1 after a brief absence.
 */
static int wait_for_reenumeration(uint32_t timeout_ms)
{
    uint32_t elapsed;

    /* Phase 1: wait for device to disconnect (CCS=0) */
    for (elapsed = 0; elapsed < timeout_ms; elapsed += 10u) {
        if (!(ehci_read(EHCI_OPR_PORTSC0) & PORTSC_CCS))
            break;
        mdelay(10);
    }
    if (elapsed >= timeout_ms) {
        diag_printf("usb_fw: re-enum: disconnect not seen within %u ms\n",
                    (unsigned)timeout_ms);
        return -1;
    }

    /* Phase 2: wait for device to reconnect (CCS=1) */
    for (elapsed = 0; elapsed < timeout_ms; elapsed += 10u) {
        if (ehci_read(EHCI_OPR_PORTSC0) & PORTSC_CCS)
            break;
        mdelay(10);
    }
    if (elapsed >= timeout_ms) {
        diag_printf("usb_fw: re-enum: reconnect not seen within %u ms\n",
                    (unsigned)timeout_ms);
        return -1;
    }

    /* Clear CSC (connect-status-change) bit — write-1-to-clear */
    {
        uint32_t portsc = ehci_read(EHCI_OPR_PORTSC0);
        ehci_write(EHCI_OPR_PORTSC0, portsc | PORTSC_CSC);
    }
    return 0;
}

/*
 * do_fw_upload — perform the full Cypress EZ-USB firmware upload sequence.
 *
 * Must be called with s_fw_mutex held.
 * Uses the blob currently in s_fw_blob[0..s_fw_blob_size-1].
 *
 * Returns USB_FW_OK on success, one of the USB_FW_ERR_* codes on failure.
 */
static int do_fw_upload(void)
{
    static const uint8_t cpu_halt[1]    = {0x01u};
    static const uint8_t cpu_release[1] = {0x00u};
    int result;

    diag_printf("usb_fw: uploading %u-byte blob via ANCHOR_LOAD_INTERNAL\n",
                (unsigned)s_fw_blob_size);

    /* Step 1: hold EZ-USB CPU in reset so we can safely overwrite its RAM */
    if (anchor_load_internal(CPUCS_ADDR, cpu_halt, 1u) < 0) {
        diag_printf("usb_fw: CPU reset-assert failed\n");
        return USB_FW_ERR_USB;
    }
    mdelay(10);

    /* Step 2: upload firmware data */
    if (s_fw_blob[0] == (uint8_t)':') {
        diag_printf("usb_fw: format: Intel HEX (HPLIP .fw)\n");
        result = ihex_upload(s_fw_blob, s_fw_blob_size);
    } else {
        diag_printf("usb_fw: format: raw binary (foo2zjs .img), "
                    "load at 0x0000\n");
        result = raw_upload(s_fw_blob, s_fw_blob_size);
    }

    /* Step 3: release CPU from reset — firmware begins running regardless
     * of whether the data upload succeeded.  This returns the device to a
     * predictable state and triggers re-enumeration. */
    if (anchor_load_internal(CPUCS_ADDR, cpu_release, 1u) < 0) {
        diag_printf("usb_fw: CPU reset-release failed\n");
        return USB_FW_ERR_USB;
    }

    if (result < 0) {
        diag_printf("usb_fw: firmware upload failed\n");
        return (s_fw_blob[0] == (uint8_t)':') ? USB_FW_ERR_FORMAT
                                               : USB_FW_ERR_USB;
    }

    diag_printf("usb_fw: upload complete — waiting for re-enumeration\n");

    /* Step 4: wait for device to reconnect with operational printer PID */
    if (wait_for_reenumeration(5000u) < 0)
        return USB_FW_ERR_TIMEOUT;

    diag_printf("usb_fw: device re-enumerated — will enumerate as printer\n");
    return USB_FW_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Public firmware blob API (used by httpd.c)
 * ───────────────────────────────────────────────────────────────────────────*/

int usb_fw_store(const uint8_t *data, size_t len)
{
    if (!data || len == 0u || len > USB_FW_MAX_SIZE)
        return -1;
    cyg_mutex_lock(&s_fw_mutex);
    memcpy(s_fw_blob, data, len);
    s_fw_blob_size = len;
    cyg_mutex_unlock(&s_fw_mutex);
    diag_printf("usb_fw: stored %u-byte firmware blob\n", (unsigned)len);
    return 0;
}

uint8_t *usb_fw_get_write_buf(size_t *max_len)
{
    /* Clear s_fw_blob_size atomically so that do_fw_upload() in the status
     * thread cannot begin reading the blob while the HTTP thread is still
     * streaming new bytes into it.  The caller must call usb_fw_commit()
     * with the final byte count once the write is complete. */
    cyg_mutex_lock(&s_fw_mutex);
    s_fw_blob_size = 0u;
    cyg_mutex_unlock(&s_fw_mutex);

    if (max_len)
        *max_len = USB_FW_MAX_SIZE;
    return s_fw_blob;
}

void usb_fw_commit(size_t len)
{
    if (len == 0u || len > USB_FW_MAX_SIZE)
        return;
    cyg_mutex_lock(&s_fw_mutex);
    s_fw_blob_size = len;
    cyg_mutex_unlock(&s_fw_mutex);
    diag_printf("usb_fw: committed %u-byte firmware blob\n", (unsigned)len);
}

int usb_fw_has_blob(void)
{
    int has;
    cyg_mutex_lock(&s_fw_mutex);
    has = (s_fw_blob_size > 0u) ? 1 : 0;
    cyg_mutex_unlock(&s_fw_mutex);
    return has;
}

size_t usb_fw_blob_size(void)
{
    size_t sz;
    cyg_mutex_lock(&s_fw_mutex);
    sz = s_fw_blob_size;
    cyg_mutex_unlock(&s_fw_mutex);
    return sz;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * USB device enumeration
 *
 * Follows the standard USB enumeration sequence:
 *   1. GET_DESCRIPTOR(DEVICE) — 8 bytes to get bMaxPacketSize0
 *   2. SET_ADDRESS(1)
 *   3. GET_DESCRIPTOR(DEVICE) — full 18 bytes
 *   4. GET_DESCRIPTOR(CONFIG) — get first configuration
 *   5. Parse interfaces looking for Printer Class (Class=7, Sub=1, Proto=2)
 *   6. Locate Bulk IN and Bulk OUT endpoints
 *   7. SET_CONFIGURATION
 * ───────────────────────────────────────────────────────────────────────────*/

/* Minimal USB descriptor structs for parsing */
#pragma pack(push, 1)
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} usb_device_desc_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} usb_config_desc_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} usb_iface_desc_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} usb_ep_desc_t;
#pragma pack(pop)

#define USB_DT_DEVICE          1
#define USB_DT_CONFIG          2
#define USB_DT_INTERFACE       4
#define USB_DT_ENDPOINT        5
#define USB_EP_DIR_IN          0x80u
#define USB_EP_XFER_BULK       0x02u
#define USB_CLASS_PRINTER      0x07u
#define USB_SUBCLASS_PRINTER   0x01u
#define USB_PROTO_UNIDIR       0x01u   /* Unidirectional (output only)     */
#define USB_PROTO_BIDIR        0x02u   /* Bi-directional protocol          */

static uint8_t s_config_buf[512] __attribute__((aligned(4)));

static int enumerate_printer(void)
{
    usb_device_desc_t *dev;
    usb_config_desc_t *cfg;
    const uint8_t     *p, *end;
    uint16_t           total_len;
    int                found_iface = 0;

    /* ── Step 1: Partial device descriptor (address still 0) ── */
    s_dev_addr = 0;
    memset(s_ctrl_buf, 0, 18);
    if (usb_get_descriptor(USB_DT_DEVICE, 0, 0, s_ctrl_buf, 8) < 0) {
        diag_printf("usb: GET_DESCRIPTOR(DEVICE,8) failed\n");
        return -1;
    }

    /* ── Step 2: Assign device address 1 ── */
    if (usb_set_address(1) < 0) {
        diag_printf("usb: SET_ADDRESS failed\n");
        return -1;
    }
    s_dev_addr = 1;
    udelay(2000); /* spec: device needs 2 ms after SET_ADDRESS */

    /* ── Step 3: Full device descriptor ── */
    memset(s_ctrl_buf, 0, 18);
    if (usb_get_descriptor(USB_DT_DEVICE, 0, 0, s_ctrl_buf, 18) < 0) {
        diag_printf("usb: GET_DESCRIPTOR(DEVICE,18) failed\n");
        return -1;
    }
    dev = (usb_device_desc_t *)s_ctrl_buf;
    diag_printf("usb: device VID=%04x PID=%04x class=%02x\n",
                (unsigned)dev->idVendor, (unsigned)dev->idProduct,
                (unsigned)dev->bDeviceClass);

    /* ── Step 3.5: Detect devices that need host-side firmware upload ──────
     *
     * Some printers do not store their firmware in non-volatile memory and
     * must receive it from the host at every power-on.  Until the firmware is
     * loaded they enumerate with a vendor-specific "stub" PID and do NOT
     * present a USB Printer Class interface.  Well-known examples:
     *
     *   HP LaserJet 1015 — VID 03f0 PID 2911 (stub) → 03f0:3315 (ready)
     *   HP LaserJet 1020 — VID 03f0 PID 2b17 (stub) → 03f0:3417 (ready)
     *   HP LaserJet 1022 — VID 03f0 PID 2c17 (stub) → 03f0:3517 (ready)
     *
     * The firmware binary is HP-proprietary and cannot be redistributed.
     * However, if the user has previously stored a firmware blob via
     * usb_fw_store() (e.g. through the web interface upload endpoint), we
     * perform the Cypress EZ-USB ANCHOR_LOAD_INTERNAL upload here automatically.
     * After a successful upload the device re-enumerates as a full Printer
     * Class device and the next status-thread poll will enumerate it normally.
     * ────────────────────────────────────────────────────────────────────── */
    {
        static const struct {
            uint16_t    vid;
            uint16_t    pid;
            const char *name;
        } needs_fw_table[] = {
            { 0x03f0u, 0x2911u, "HP LaserJet 1015" },
            { 0x03f0u, 0x2b17u, "HP LaserJet 1020" },
            { 0x03f0u, 0x2c17u, "HP LaserJet 1022" },
            { 0, 0, NULL }
        };
        unsigned i;
        for (i = 0; needs_fw_table[i].name != NULL; i++) {
            if (dev->idVendor  == needs_fw_table[i].vid &&
                dev->idProduct == needs_fw_table[i].pid) {
                diag_printf("usb: %s detected (VID=%04x PID=%04x, "
                            "pre-firmware stub)\n",
                            needs_fw_table[i].name,
                            (unsigned)dev->idVendor,
                            (unsigned)dev->idProduct);

                /* Attempt automatic upload if a firmware blob is stored */
                cyg_mutex_lock(&s_fw_mutex);
                if (s_fw_blob_size > 0u) {
                    int fw_result = do_fw_upload();
                    cyg_mutex_unlock(&s_fw_mutex);
                    if (fw_result == USB_FW_OK) {
                        /* Device re-enumerated; status_thread will pick it up
                         * on the next poll and call enumerate_printer() again.
                         * Return -1 here so the current attempt exits cleanly.*/
                        g_printer_status.needs_firmware = false;
                        return -1;
                    }
                    diag_printf("usb_fw: automatic upload failed (err=%d)\n",
                                fw_result);
                } else {
                    cyg_mutex_unlock(&s_fw_mutex);
                }

                g_printer_status.needs_firmware = true;
                diag_printf(
                    "usb: No firmware blob stored.\n"
                    "usb: Option A — pre-load via Windows PC (HP driver) or\n"
                    "usb:            Linux/macOS host (HPLIP), then reconnect.\n"
                    "usb: Option B — upload firmware blob to this print server:\n"
                    "usb:   curl -X POST http://<ip>/api/upload_printer_fw "
                            "--data-binary @hp_laserjet_1020.fw\n"
                    "usb:   (see COMPATIBILITY.md for full instructions)\n");
                return -1;
            }
        }
    }

    /* ── Step 4: Configuration descriptor (first 9 bytes to get wTotalLength) ── */
    memset(s_config_buf, 0, sizeof(s_config_buf));
    if (usb_get_descriptor(USB_DT_CONFIG, 0, 0, s_config_buf, 9) < 0) {
        diag_printf("usb: GET_DESCRIPTOR(CONFIG,9) failed\n");
        return -1;
    }
    cfg = (usb_config_desc_t *)s_config_buf;
    total_len = cfg->wTotalLength;
    if (total_len > (uint16_t)sizeof(s_config_buf))
        total_len = (uint16_t)sizeof(s_config_buf);
    s_config_value = cfg->bConfigurationValue;

    /* Full configuration descriptor */
    memset(s_config_buf, 0, sizeof(s_config_buf));
    if (usb_get_descriptor(USB_DT_CONFIG, 0, 0, s_config_buf, total_len) < 0) {
        diag_printf("usb: GET_DESCRIPTOR(CONFIG,full) failed\n");
        return -1;
    }

    /* ── Step 5+6: Walk descriptors looking for Printer Class interface ── */
    p   = s_config_buf + 9; /* skip config descriptor */
    end = s_config_buf + total_len;

    while (p < end) {
        uint8_t bLen  = p[0];
        uint8_t bType = p[1];

        if (bLen < 2 || p + bLen > end)
            break;

        if (bType == USB_DT_INTERFACE) {
            const usb_iface_desc_t *iface = (const usb_iface_desc_t *)p;
            if (iface->bInterfaceClass    == USB_CLASS_PRINTER &&
                iface->bInterfaceSubClass == USB_SUBCLASS_PRINTER &&
                (iface->bInterfaceProtocol == USB_PROTO_BIDIR ||
                 iface->bInterfaceProtocol == USB_PROTO_UNIDIR)) {
                found_iface = 1;
                diag_printf("usb: found %s Printer interface\n",
                            iface->bInterfaceProtocol == USB_PROTO_BIDIR ?
                            "bi-directional" : "unidirectional");
            } else {
                found_iface = 0;
            }
        } else if (bType == USB_DT_ENDPOINT && found_iface) {
            const usb_ep_desc_t *ep = (const usb_ep_desc_t *)p;
            if ((ep->bmAttributes & 0x03u) == USB_EP_XFER_BULK) {
                if (ep->bEndpointAddress & USB_EP_DIR_IN) {
                    s_ep_bulk_in       = ep->bEndpointAddress & 0x0Fu;
                    s_max_pkt_bulk_in  = ep->wMaxPacketSize & 0x7FFu;
                    diag_printf("usb: bulk IN  ep=0x%02x maxpkt=%u\n",
                                (unsigned)ep->bEndpointAddress,
                                (unsigned)s_max_pkt_bulk_in);
                } else {
                    s_ep_bulk_out      = ep->bEndpointAddress & 0x0Fu;
                    s_max_pkt_bulk_out = ep->wMaxPacketSize & 0x7FFu;
                    diag_printf("usb: bulk OUT ep=0x%02x maxpkt=%u\n",
                                (unsigned)ep->bEndpointAddress,
                                (unsigned)s_max_pkt_bulk_out);
                }
            }
        }
        p += bLen;
    }

    if (!s_ep_bulk_out) {
        diag_printf("usb: no Printer Class bulk OUT endpoint found\n");
        return -1;
    }
    /* Bulk IN is optional (unidirectional printers only have Bulk OUT),
     * but without it we cannot provide bi-directional status feedback. */
    if (!s_ep_bulk_in) {
        diag_printf("usb: no bulk IN endpoint — back-channel unavailable\n");
    }

    /* ── Step 7: SET_CONFIGURATION ── */
    if (usb_set_configuration(s_config_value) < 0) {
        diag_printf("usb: SET_CONFIGURATION failed\n");
        return -1;
    }

    /* Reset data toggles */
    s_dt_out = 0;
    s_dt_in  = 0;

    diag_printf("usb: printer enumerated OK (addr=%u)\n",
                (unsigned)s_dev_addr);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * USB Printer Class control requests
 * ───────────────────────────────────────────────────────────────────────────*/

int usb_printer_get_port_status(uint8_t *status_byte)
{
    uint8_t setup[8] = {
        0xA1,                /* bmRequestType: IN, Class, Interface  */
        0x01,                /* bRequest: GET_PORT_STATUS            */
        0x00, 0x00,          /* wValue                               */
        USB_PRINTER_IFACE, 0,/* wIndex: interface number             */
        0x01, 0x00,          /* wLength: 1 byte                      */
    };
    uint8_t status_buf[4] __attribute__((aligned(4))) = {0};

    if (!s_initialised || !g_printer_status.connected)
        return -1;

    if (usb_control_transfer(setup, status_buf, 1, true) < 0)
        return -1;

    *status_byte = status_buf[0];
    return 0;
}

int usb_printer_get_device_id(char *buf, size_t max_len)
{
    uint8_t setup[8] = {
        0xA1,                /* bmRequestType: IN, Class, Interface  */
        0x00,                /* bRequest: GET_DEVICE_ID              */
        0x00, 0x00,          /* wValue: Configuration index (0)      */
        USB_PRINTER_IFACE, 0,/* wIndex: interface number             */
        0x00, 0x02,          /* wLength: 512 bytes (big-endian 0x200)*/
    };
    uint8_t id_buf[512] __attribute__((aligned(4)));
    uint16_t id_len;
    size_t   copy_len;

    if (!s_initialised || !g_printer_status.connected)
        return -1;

    memset(id_buf, 0, sizeof(id_buf));
    if (usb_control_transfer(setup, id_buf, 512u, true) < 0)
        return -1;

    /* The first two bytes of the Device ID response are a big-endian length
     * field (including those two bytes themselves). */
    id_len = (uint16_t)((id_buf[0] << 8) | id_buf[1]);
    if (id_len < 2u || id_len > 512u)
        id_len = 2u;

    copy_len = (size_t)(id_len - 2u);
    if (copy_len >= max_len)
        copy_len = max_len - 1u;

    memcpy(buf, id_buf + 2, copy_len);
    buf[copy_len] = '\0';
    return 0;
}

int usb_printer_soft_reset(void)
{
    uint8_t setup[8] = {
        0x21,                /* bmRequestType: OUT, Class, Interface */
        0x02,                /* bRequest: SOFT_RESET                 */
        0x00, 0x00,          /* wValue                               */
        USB_PRINTER_IFACE, 0,/* wIndex: interface number             */
        0x00, 0x00,          /* wLength: 0                           */
    };

    if (!s_initialised || !g_printer_status.connected)
        return -1;

    return usb_control_transfer(setup, NULL, 0, false);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────────────*/

int usb_printer_init(void)
{
    memset((void *)&g_printer_status, 0, sizeof(g_printer_status));

    /* Initialise the firmware-blob mutex (before any possible upload) */
    cyg_mutex_init(&s_fw_mutex);

    /* If the firmware was baked in at build time, pre-populate the blob
     * buffer so stub-PID printers (HP LJ 1015/1020/1022) are handled
     * automatically without any web-UI upload step. */
#ifdef HAVE_HP1020_FW_BUILTIN
    if (HP1020_FW_BLOB_SIZE > 0u &&
        HP1020_FW_BLOB_SIZE <= USB_FW_MAX_SIZE) {
        memcpy(s_fw_blob, hp1020_fw_blob, HP1020_FW_BLOB_SIZE);
        s_fw_blob_size = HP1020_FW_BLOB_SIZE;
        diag_printf("usb_fw: built-in blob loaded (%u bytes)\n",
                    (unsigned)HP1020_FW_BLOB_SIZE);
    }
#endif

    diag_printf("usb: initialising MT7688 USB host controller\n");

    platform_usb_init();

    if (ehci_init() < 0) {
        diag_printf("usb: EHCI init failed\n");
        return -1;
    }

    s_initialised = true;

    /* Try to enumerate a printer that is already connected */
    if (ehci_port_reset() == 0) {
        if (enumerate_printer() == 0) {
            g_printer_status.connected = true;
            /* Fetch initial status and Device ID */
            usb_printer_update_status();
        }
    }

    return 0;
}

int usb_printer_write(const uint8_t *data, size_t len)
{
    int result;

    if (!s_initialised || !g_printer_status.connected)
        return 0;
    if (!data || len == 0)
        return 0;

    g_printer_status.busy = true;
    result = usb_bulk_transfer(false, (void *)data, len, 10000u);
    if (result > 0) {
        g_printer_status.bytes_sent += (uint32_t)result;
    }
    /* busy flag is cleared by status_thread after next GET_PORT_STATUS */
    return result;
}

int usb_printer_read(uint8_t *buf, size_t max_len, uint32_t timeout_ms)
{
    if (!s_initialised || !g_printer_status.connected)
        return 0;
    if (!s_ep_bulk_in)
        return 0;   /* printer has no back-channel endpoint */
    if (!buf || max_len == 0)
        return 0;

    return usb_bulk_transfer(true, buf, max_len, timeout_ms);
}

void usb_printer_update_status(void)
{
    uint8_t status_byte = 0;

    if (!s_initialised)
        return;

    /* Re-check connection state via PORTSC */
    {
        uint32_t portsc = ehci_read(EHCI_OPR_PORTSC0);
        if (!(portsc & PORTSC_CCS)) {
            /* Device disconnected — clear all state including needs_firmware */
            if (g_printer_status.connected || g_printer_status.needs_firmware) {
                diag_printf("usb: USB device disconnected\n");
                memset((void *)&g_printer_status, 0,
                       sizeof(g_printer_status));
            }
            return;
        }
        if (!g_printer_status.connected) {
            /* A USB device is physically present but not yet enumerated as a
             * printer.  If we already know it needs host-side firmware, skip
             * re-enumeration — the device's PID will not change until it is
             * power-cycled after receiving firmware from a host PC.  Only
             * attempt enumeration again after a physical disconnect/reconnect,
             * which clears needs_firmware via the branch above. */
            if (!g_printer_status.needs_firmware) {
                diag_printf("usb: USB device connected — enumerating\n");
                if (ehci_port_reset() == 0 && enumerate_printer() == 0) {
                    g_printer_status.connected = true;
                    diag_printf("usb: printer ready\n");
                }
            }
            return;
        }
    }

    /* GET_PORT_STATUS — the bi-directional status back-channel */
    if (usb_printer_get_port_status(&status_byte) == 0) {
        g_printer_status.raw_status  = status_byte;
        g_printer_status.online      = (status_byte & USB_PRINTER_STS_SELECT)    != 0;
        g_printer_status.paper_empty = (status_byte & USB_PRINTER_STS_PAPER_EMPTY) != 0;
        g_printer_status.error       = (status_byte & USB_PRINTER_STS_NOT_ERROR) == 0;

        /* Clear busy flag if printer is now idle (no error, not processing) */
        if (g_printer_status.online && !g_printer_status.error)
            g_printer_status.busy = false;
    }

    /* Fetch Device ID once (the first time we have a connected printer) */
    if (g_printer_status.connected && g_printer_status.device_id[0] == '\0') {
        usb_printer_get_device_id((char *)g_printer_status.device_id,
                                  sizeof(g_printer_status.device_id));
        if (g_printer_status.device_id[0] != '\0') {
            diag_printf("usb: Device ID: %.80s\n",
                        (const char *)g_printer_status.device_id);
        }
    }
}

cyg_bool_t usb_printer_is_connected(void)
{
    return g_printer_status.connected;
}
