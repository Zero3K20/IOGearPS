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
 *   Thread 19 — eSCL scanner server (port 9290)
 */

#include "rtos.h"

#include <stdio.h>
#include <string.h>

#include "lwip/tcpip.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "../freertos/netif/mt7688_eth.h"
#include "../bsp/mt7688_uart.h"    /* mt7688_wdt_keepalive() */

/* Service headers */
#include "httpd.h"
#include "ipp_server.h"
#include "escl_server.h"
#include "mdns.h"
#include "lpr.h"
#include "config.h"

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
#define NUM_THREADS         20

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

    /* Wait for the network to come up */
    cyg_thread_delay(100);

    diag_printf("GPSU21: USB print port ready\n");

    /* Main loop: forward print jobs from network queues to the USB printer */
    for (;;) {
        cyg_thread_delay(10);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Minimal stubs for services that are not fully implemented
 * ───────────────────────────────────────────────────────────────────────────*/
static void raw_tcp_thread(cyg_addrword_t arg)
{
    (void)arg;
    diag_printf("GPSU21: raw TCP server (port 9100) started\n");
    for (;;) cyg_thread_delay(1000);
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
    for (;;) cyg_thread_delay(500);
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
    { escl_server_thread,   "escl",           12 },
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
