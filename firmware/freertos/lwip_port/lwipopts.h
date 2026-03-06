/*
 * lwipopts.h — lwIP configuration for the IOGear GPSU21 FreeRTOS firmware.
 *
 * Tuned to match the connection-pool limits of the original ZOT firmware
 * (36 simultaneous TCP connections, as in the eCos ecos.ecc configuration).
 */

#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* ── OS mode ────────────────────────────────────────────────────────────── */
/* Use FreeRTOS for sys_arch (threads, semaphores, mailboxes). */
#define NO_SYS                          0

/* ── Socket API ─────────────────────────────────────────────────────────── */
#define LWIP_SOCKET                     1
#define LWIP_NETCONN                    1
/* Use lwip_xxx() prefixed socket calls (not the POSIX-named aliases). */
#define LWIP_COMPAT_SOCKETS             0

/* ── IPv4 ────────────────────────────────────────────────────────────────── */
#define LWIP_IPV4                       1
#define LWIP_IPV6                       0

/* ── ICMP / ARP / IGMP ───────────────────────────────────────────────────── */
#define LWIP_ICMP                       1
#define LWIP_ARP                        1
#define LWIP_IGMP                       1   /* required for mDNS multicast  */

/* ── TCP ─────────────────────────────────────────────────────────────────── */
#define LWIP_TCP                        1
#define MEMP_NUM_TCP_PCB                36  /* match original firmware pool  */
#define TCP_MSS                         1460
#define TCP_WND                         (4 * TCP_MSS)
#define TCP_SND_BUF                     (4 * TCP_MSS)

/* ── UDP ─────────────────────────────────────────────────────────────────── */
#define LWIP_UDP                        1

/* ── DHCP ────────────────────────────────────────────────────────────────── */
#define LWIP_DHCP                       1

/* ── DNS ─────────────────────────────────────────────────────────────────── */
#define LWIP_DNS                        0

/* ── Memory pools ────────────────────────────────────────────────────────── */
#define MEM_SIZE                        (65536)
#define MEMP_NUM_NETCONN                36
#define PBUF_POOL_SIZE                  64

/* ── Stats / debug ───────────────────────────────────────────────────────── */
#define LWIP_STATS                      0
#define LWIP_DEBUG                      0

/* ── Prevent struct timeval redefinition (newlib provides it) ─────────────── */
#define LWIP_TIMEVAL_PRIVATE                0

/* ── Sequential API (netconn) thread stack ───────────────────────────────── */
#define TCPIP_THREAD_NAME               "tcpip"
#define TCPIP_THREAD_STACKSIZE          4096
#define TCPIP_THREAD_PRIO               (configMAX_PRIORITIES - 2)
#define TCPIP_MBOX_SIZE                 32
#define DEFAULT_TCP_RECVMBOX_SIZE       16
#define DEFAULT_UDP_RECVMBOX_SIZE       8
#define DEFAULT_ACCEPTMBOX_SIZE         8
#define DEFAULT_THREAD_STACKSIZE        2048
#define DEFAULT_THREAD_PRIO             (configMAX_PRIORITIES / 2)

/* ── Checksum offload (none — computed in software) ──────────────────────── */
#define CHECKSUM_GEN_IP                 1
#define CHECKSUM_GEN_UDP                1
#define CHECKSUM_GEN_TCP                1
#define CHECKSUM_CHECK_IP               1
#define CHECKSUM_CHECK_UDP              1
#define CHECKSUM_CHECK_TCP              1

#endif /* LWIPOPTS_H */
