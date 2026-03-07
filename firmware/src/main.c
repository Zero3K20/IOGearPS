/*
 * main.c — IOGear GPSU21 print-server application entry point.
 *
 * This is the eCos application that runs on the MediaTek MT7688 SoC inside
 * the IOGear GPSU21 print server.  It mirrors the thread structure of the
 * original ZOT firmware binary (19 concurrent service threads) while
 * incorporating the stability fixes from tools/patch_gpsu21.py.
 *
 * Entry point: main() → cyg_user_start() → vTaskStartScheduler()
 *
 * Thread layout (same as stock firmware):
 *   Thread 0  — main print-server loop / USB management
 *   Thread 1  — HTTP server (port 80)
 *   Thread 2  — IPP server (port 631)
 *   Thread 3  — LPR/LPD server (port 515)
 *   Thread 4  — Raw TCP print server (port 9100)
 *   Thread 5  — SMB/Windows printing (port 139/445)
 *   Thread 6  — mDNS/Bonjour responder
 *   Thread 7  — NetBIOS name service
 *   Thread 8  — SNMP agent (port 161)
 *   Thread 9  — Telnet management CLI (port 23)
 *   Thread 10 — TFTP server (port 69)
 *   Thread 11 — Email alert
 *   Thread 12 — AppleTalk/PAP server
 *   Thread 13 — Novell SAP broadcast
 *   Thread 14 — Novell NDS client
 *   Thread 15 — NetWare Bindery client
 *   Thread 16 — Status polling
 *   Thread 17 — Network watchdog / keep-alive
 *   Thread 18 — Idle / diagnostic
 */

#include "rtos.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "lwip/tcpip.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include "../freertos/netif/mt7688_eth.h"
#include "../bsp/mt7688_uart.h"    /* mt7688_wdt_keepalive() */

/* Service headers */
#include "httpd.h"
#include "ipp_server.h"
#include "mdns.h"
#include "lpr.h"
#include "config.h"
#include "usb_printer.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Build-time firmware version string — embedded at a fixed offset so that
 * tools/unpack_gpsu21.py can find it at offset 0x28 in the .bin file.
 * The format must be: MT7688-<major>.<minor>.<rel>.<build>.<serial>-<date> <time>
 * ───────────────────────────────────────────────────────────────────────────*/
const char __attribute__((used, section(".version")))
    gpsu21_version[] = "MT7688-9.09.56.9034.00001243t-2019/11/19 13:00:10";

/* ─────────────────────────────────────────────────────────────────────────────
 * Thread stacks and control structures
 * ───────────────────────────────────────────────────────────────────────────*/
#define THREAD_STACK_SIZE   8192
#define NUM_THREADS         19

static cyg_handle_t thread_handles[NUM_THREADS];
static cyg_thread   thread_objs[NUM_THREADS];
static char         thread_stacks[NUM_THREADS][THREAD_STACK_SIZE];

/* ─────────────────────────────────────────────────────────────────────────────
 * Forward declarations for threads not defined in separate .c files
 * ───────────────────────────────────────────────────────────────────────────*/
static void raw_tcp_thread(cyg_addrword_t arg);
static void smb_thread(cyg_addrword_t arg);
static void netbios_thread(cyg_addrword_t arg);
static void snmp_thread(cyg_addrword_t arg);
static void telnet_thread(cyg_addrword_t arg);
static void tftp_thread(cyg_addrword_t arg);
static void email_alert_thread(cyg_addrword_t arg);
static void appletalk_thread(cyg_addrword_t arg);
static void netware_sap_thread(cyg_addrword_t arg);
static void netware_nds_thread(cyg_addrword_t arg);
static void netware_bindery_thread(cyg_addrword_t arg);
static void status_thread(cyg_addrword_t arg);
static void watchdog_thread(cyg_addrword_t arg);
static void idle_thread(cyg_addrword_t arg);

/* ─────────────────────────────────────────────────────────────────────────────
 * Main print-server loop
 * ───────────────────────────────────────────────────────────────────────────*/
static void print_server_main(cyg_addrword_t arg)
{
    (void)arg;
    diag_printf("GPSU21: print-server main thread started\n");

    /* Wait for the network stack and all service threads to start */
    cyg_thread_delay(pdMS_TO_TICKS(500));

    /* Initialise the USB host controller and enumerate any attached printer.
     * usb_printer_init() also performs an initial GET_PORT_STATUS and
     * GET_DEVICE_ID so that g_printer_status is populated before the first
     * client connection arrives. */
    if (usb_printer_init() == 0) {
        diag_printf("GPSU21: USB host controller ready\n");
        if (usb_printer_is_connected()) {
            diag_printf("GPSU21: USB printer connected and enumerated\n");
        } else {
            diag_printf("GPSU21: USB host ready — waiting for printer\n");
        }
    } else {
        diag_printf("GPSU21: USB host init failed — printing disabled\n");
    }

    /* Main thread is no longer needed once USB is initialised.
     * Status polling and USB hotplug detection are handled by status_thread. */
    for (;;) {
        cyg_thread_delay(pdMS_TO_TICKS(5000));
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Minimal stubs for services that are not fully implemented
 * ───────────────────────────────────────────────────────────────────────────*/
/* ─────────────────────────────────────────────────────────────────────────────
 * Raw TCP print server — port 9100 (JetDirect / AppSocket protocol)
 *
 * The AppSocket protocol (also called JetDirect or raw TCP printing) is the
 * simplest possible network printing protocol:
 *   - Client connects to port 9100
 *   - Client sends raw print data (PostScript, PCL, PDF, etc.)
 *   - Server forwards it byte-for-byte to the USB printer
 *   - Server reads any back-channel data the printer sends (Bulk IN) and
 *     forwards it back over the same TCP socket — this is the bi-directional
 *     leg that allows the client to receive PJL status responses, @PJL INFO
 *     replies, or raw status bytes from the printer
 * ───────────────────────────────────────────────────────────────────────────*/
#define RAW_TCP_PORT          9100
#define RAW_TCP_BUF_SIZE      4096
#define RAW_TCP_BACK_BUF_SIZE 256   /* back-channel read size per poll       */
#define RAW_TCP_BACK_TIMEOUT  50    /* ms to wait for back-channel data      */

static void raw_tcp_handle_connection(int fd)
{
    uint8_t *fwd_buf;
    uint8_t *back_buf;
    int      rcv;

    fwd_buf  = (uint8_t *)malloc(RAW_TCP_BUF_SIZE);
    back_buf = (uint8_t *)malloc(RAW_TCP_BACK_BUF_SIZE);

    if (!fwd_buf || !back_buf) {
        diag_printf("raw_tcp: out of memory\n");
        free(fwd_buf);
        free(back_buf);
        return;
    }

    /* Set a short socket receive timeout (100 ms) so we can poll the
     * back-channel while waiting for more data from the client. */
    {
        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 100000; /* 100 ms */
        lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    for (;;) {
        /* ── Forward channel: network → USB printer ─────────────────────── */
        rcv = lwip_recv(fd, fwd_buf, RAW_TCP_BUF_SIZE, 0);
        if (rcv < 0) {
            /* EAGAIN / EWOULDBLOCK — timeout, no data yet; check back-channel */
        } else if (rcv == 0) {
            /* Connection closed by client — flush back-channel and exit */
            break;
        } else {
            /* Forward data to the USB printer */
            if (usb_printer_write(fwd_buf, (size_t)rcv) < 0) {
                diag_printf("raw_tcp: USB write error\n");
                break;
            }
            g_printer_status.busy = true;
        }

        /* ── Back channel: USB printer → network ────────────────────────── *
         * Poll the printer's Bulk IN endpoint.  Any data returned is sent
         * back to the client over the same TCP connection.  This allows PJL
         * or other back-channel protocols to work transparently. */
        if (usb_printer_is_connected()) {
            int back = usb_printer_read(back_buf, RAW_TCP_BACK_BUF_SIZE,
                                        RAW_TCP_BACK_TIMEOUT);
            if (back > 0) {
                lwip_send(fd, back_buf, (size_t)back, 0);
            }
        }
    }

    /* Drain any remaining back-channel data before closing */
    if (usb_printer_is_connected()) {
        int back;
        while ((back = usb_printer_read(back_buf, RAW_TCP_BACK_BUF_SIZE,
                                        RAW_TCP_BACK_TIMEOUT)) > 0) {
            lwip_send(fd, back_buf, (size_t)back, 0);
        }
    }

    free(fwd_buf);
    free(back_buf);
}

typedef struct {
    int          fd;
    cyg_bool_t   in_use;
    cyg_handle_t thread;
} raw_conn_t;

#define RAW_TCP_MAX_CONNECTIONS 4
#define RAW_TCP_THREAD_STACK    4096
#define RAW_TCP_THREAD_PRIO     12

static raw_conn_t  raw_pool[RAW_TCP_MAX_CONNECTIONS];
static cyg_mutex_t raw_pool_lock;

static void raw_tcp_child_thread(cyg_addrword_t arg)
{
    raw_conn_t *conn = (raw_conn_t *)arg;
    raw_tcp_handle_connection(conn->fd);
    lwip_close(conn->fd);
    cyg_mutex_lock(&raw_pool_lock);
    conn->in_use = false;
    cyg_mutex_unlock(&raw_pool_lock);
    cyg_thread_exit();
}

static void raw_tcp_thread(cyg_addrword_t arg)
{
    int                server_fd;
    int                client_fd;
    struct sockaddr_in addr;
    int                opt = 1;
    cyg_uint32         i;

    (void)arg;

    cyg_mutex_init(&raw_pool_lock);
    memset(raw_pool, 0, sizeof(raw_pool));

    server_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        diag_printf("raw_tcp: socket() failed\n");
        return;
    }
    lwip_setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(RAW_TCP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (lwip_bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        diag_printf("raw_tcp: bind() failed\n");
        lwip_close(server_fd);
        return;
    }
    if (lwip_listen(server_fd, RAW_TCP_MAX_CONNECTIONS) < 0) {
        diag_printf("raw_tcp: listen() failed\n");
        lwip_close(server_fd);
        return;
    }

    diag_printf("raw_tcp: listening on port %d (bi-directional)\n", RAW_TCP_PORT);

    for (;;) {
        raw_conn_t *slot = NULL;

        client_fd = lwip_accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            cyg_thread_delay(10);
            continue;
        }

        cyg_mutex_lock(&raw_pool_lock);
        for (i = 0; i < RAW_TCP_MAX_CONNECTIONS; i++) {
            if (!raw_pool[i].in_use) {
                slot = &raw_pool[i];
                slot->in_use = true;
                slot->fd     = client_fd;
                break;
            }
        }
        cyg_mutex_unlock(&raw_pool_lock);

        if (!slot) {
            diag_printf("raw_tcp: no free connection slot\n");
            lwip_close(client_fd);
            continue;
        }

        {
            BaseType_t ret = xTaskCreate(
                (TaskFunction_t)raw_tcp_child_thread,
                "raw_child",
                (configSTACK_DEPTH_TYPE)(RAW_TCP_THREAD_STACK / sizeof(StackType_t)),
                (void *)slot,
                CYG_TO_FRT_PRIO(RAW_TCP_THREAD_PRIO),
                &slot->thread);
            if (ret != pdPASS) {
                diag_printf("raw_tcp: xTaskCreate failed\n");
                lwip_close(client_fd);
                cyg_mutex_lock(&raw_pool_lock);
                slot->in_use = false;
                cyg_mutex_unlock(&raw_pool_lock);
            }
        }
    }
}

static void smb_thread(cyg_addrword_t arg)
{
    (void)arg;
    diag_printf("GPSU21: SMB server started\n");
    for (;;) cyg_thread_delay(1000);
}

static void netbios_thread(cyg_addrword_t arg)
{
    (void)arg;
    diag_printf("GPSU21: NetBIOS name service started\n");
    for (;;) cyg_thread_delay(1000);
}

static void snmp_thread(cyg_addrword_t arg)
{
    (void)arg;
    diag_printf("GPSU21: SNMP agent started\n");
    for (;;) cyg_thread_delay(1000);
}

static void telnet_thread(cyg_addrword_t arg)
{
    (void)arg;
    diag_printf("GPSU21: Telnet CLI started\n");
    for (;;) cyg_thread_delay(1000);
}

static void tftp_thread(cyg_addrword_t arg)
{
    (void)arg;
    diag_printf("GPSU21: TFTP server started\n");
    for (;;) cyg_thread_delay(1000);
}

static void email_alert_thread(cyg_addrword_t arg)
{
    (void)arg;
    diag_printf("GPSU21: email alert thread started\n");
    for (;;) cyg_thread_delay(1000);
}

static void appletalk_thread(cyg_addrword_t arg)
{
    (void)arg;
    diag_printf("GPSU21: AppleTalk/PAP server started\n");
    for (;;) cyg_thread_delay(1000);
}

static void netware_sap_thread(cyg_addrword_t arg)
{
    (void)arg;
    diag_printf("GPSU21: NetWare SAP broadcast thread started\n");
    for (;;) cyg_thread_delay(1000);
}

static void netware_nds_thread(cyg_addrword_t arg)
{
    (void)arg;
    diag_printf("GPSU21: NetWare NDS client started\n");
    for (;;) cyg_thread_delay(1000);
}

static void netware_bindery_thread(cyg_addrword_t arg)
{
    (void)arg;
    diag_printf("GPSU21: NetWare Bindery client started\n");
    for (;;) cyg_thread_delay(1000);
}

static void status_thread(cyg_addrword_t arg)
{
    (void)arg;
    diag_printf("GPSU21: status polling thread started\n");

    /* Wait for USB to be initialised by print_server_main before polling.
     * print_server_main has priority 10 (higher) so it runs first; still
     * add a small delay to be safe. */
    cyg_thread_delay(pdMS_TO_TICKS(2000));

    for (;;) {
        /* Poll USB printer state via GET_PORT_STATUS (back-channel).
         * This updates g_printer_status.online, .paper_empty, .error and
         * detects hotplug connect/disconnect events. */
        usb_printer_update_status();

        /* Poll every 2 seconds — frequent enough for responsive status
         * reporting without overloading the USB host controller. */
        cyg_thread_delay(pdMS_TO_TICKS(2000));
    }
}

static void watchdog_thread(cyg_addrword_t arg)
{
    (void)arg;
    diag_printf("GPSU21: watchdog thread started — MT7688 WDT kept disabled\n");

    /*
     * Keep the MT7688 hardware watchdog disabled by writing 0 to its timer
     * register every second.  The WDT was already disabled in board_init()
     * via mt7688_wdt_disable(); this loop ensures it cannot be accidentally
     * re-armed by any other code path for the lifetime of the firmware.
     *
     * NOTE: A production firmware should instead arm the WDT with a suitable
     * timeout (e.g. 5 s) and kick it here so the hardware can recover from
     * a genuine firmware hang.  The current implementation prioritises
     * correctness and non-bricking over hardware-watchdog protection.
     */
    for (;;) {
        mt7688_wdt_keepalive();
        cyg_thread_delay(pdMS_TO_TICKS(500));  /* every 500 ms */
    }
}

static void idle_thread(cyg_addrword_t arg)
{
    (void)arg;
    for (;;) cyg_thread_delay(10000);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Thread descriptors (index, function, name, priority)
 * Priority: 10 = highest used; 30 = idle.  eCos default scheduler range is
 * 0–31.  These values match the priorities visible in the stock firmware's
 * thread debug output.
 * ───────────────────────────────────────────────────────────────────────────*/
typedef struct {
    cyg_thread_entry_t *fn;
    const char         *name;
    cyg_ucount32        priority;
} thread_desc_t;

static const thread_desc_t thread_descs[NUM_THREADS] = {
    { print_server_main,    "main",           10 },
    { httpd_thread,         "httpd",          12 },
    { ipp_server_thread,    "ipp",            12 },
    { lpr_thread,           "lpr",            12 },
    { raw_tcp_thread,       "raw_tcp",        12 },
    { smb_thread,           "smb",            13 },
    { mdns_thread,          "mdns",           11 },
    { netbios_thread,       "netbios",        14 },
    { snmp_thread,          "snmp",           15 },
    { telnet_thread,        "telnet",         16 },
    { tftp_thread,          "tftp",           16 },
    { email_alert_thread,   "email_alert",    18 },
    { appletalk_thread,     "appletalk",      14 },
    { netware_sap_thread,   "nw_sap",         17 },
    { netware_nds_thread,   "nw_nds",         17 },
    { netware_bindery_thread,"nw_bindery",    17 },
    { status_thread,        "status",         20 },
    { watchdog_thread,      "watchdog",       10 },
    { idle_thread,          "idle",           30 },
};

/* ─────────────────────────────────────────────────────────────────────────────
 * Network interface (lwIP)
 * ───────────────────────────────────────────────────────────────────────────*/
static struct netif gpsu21_netif;

static void netif_setup(void *arg)
{
    ip4_addr_t ipaddr, netmask, gw;
    (void)arg;

    IP4_ADDR(&ipaddr,  MT7688_IP_ADDR0, MT7688_IP_ADDR1,
                       MT7688_IP_ADDR2, MT7688_IP_ADDR3);
    IP4_ADDR(&netmask, MT7688_NETMASK0, MT7688_NETMASK1,
                       MT7688_NETMASK2, MT7688_NETMASK3);
    IP4_ADDR(&gw,      MT7688_GW_ADDR0, MT7688_GW_ADDR1,
                       MT7688_GW_ADDR2, MT7688_GW_ADDR3);

    netif_add(&gpsu21_netif, &ipaddr, &netmask, &gw,
              NULL, mt7688_eth_init, tcpip_input);
    netif_set_default(&gpsu21_netif);
    netif_set_up(&gpsu21_netif);

    /* Request an address via DHCP (falls back to static if no server). */
    dhcp_start(&gpsu21_netif);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * cyg_user_start() — application entry point.
 *
 * Initialises lwIP, creates all service threads, and returns.
 * The FreeRTOS scheduler (started in main()) takes over from here.
 * ───────────────────────────────────────────────────────────────────────────*/
void cyg_user_start(void)
{
    cyg_uint32 i;

    /* Initialise runtime configuration (device name, AirPrint flag). */
    config_init();

    diag_printf("\n");
    diag_printf("========================================\n");
    diag_printf(" IOGear GPSU21 Print Server\n");
    diag_printf(" FreeRTOS firmware — %s\n", gpsu21_version);
    diag_printf("========================================\n");

    /* Initialise the lwIP TCP/IP stack (starts the tcpip_thread). */
    tcpip_init(netif_setup, NULL);

    for (i = 0; i < NUM_THREADS; i++) {
        cyg_thread_create(
            thread_descs[i].priority,
            thread_descs[i].fn,
            (cyg_addrword_t)i,
            (char *)thread_descs[i].name,
            thread_stacks[i],
            THREAD_STACK_SIZE,
            &thread_handles[i],
            &thread_objs[i]
        );
        cyg_thread_resume(thread_handles[i]);
    }

    diag_printf("GPSU21: %u service threads created\n", NUM_THREADS);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * main() — FreeRTOS entry point.
 *
 * Called from startup.S after BSS clear and board_init().
 * Creates the application tasks via cyg_user_start(), then hands control to
 * the FreeRTOS scheduler (never returns).
 * ───────────────────────────────────────────────────────────────────────────*/
int main(void)
{
    cyg_user_start();
    vTaskStartScheduler();
    /* Should never reach here — scheduler runs forever. */
    for (;;) { /* never reached */ }
    return 0;
}
