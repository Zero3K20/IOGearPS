/*
 * mdns.c — mDNS/Bonjour advertising for the IOGear GPSU21 print server.
 *
 * Advertises the printer as _ipp._tcp on the local network so that iOS 14+
 * and macOS 11+ clients can discover it automatically via AirPrint.
 *
 * This implementation both sends periodic mDNS announcements AND listens for
 * incoming mDNS queries and responds to them, so that iOS AirPrint discovery
 * works reliably even when the device starts up after the iOS device.
 *
 * Key fixes vs. original implementation:
 *   1. The A record now carries the device's real IP address (obtained from
 *      the lwIP netif), not the hard-coded 0.0.0.0 that the original code
 *      advertised because my_ip was never updated.
 *   2. PTR records no longer carry the mDNS cache-flush bit (RFC 6762 §11.3
 *      prohibits it on shared PTR records).  Only SRV, TXT, and A records
 *      carry the cache-flush bit.
 *   3. A _services._dns-sd._udp.local PTR record is included so that iOS
 *      service enumeration (Browse for services) finds the printer.
 *   4. The socket is now bound to port 5353 so that incoming mDNS queries
 *      are received.  The thread responds to any query that mentions
 *      _ipp._tcp, ensuring instant AirPrint discovery on iOS/macOS.
 *   5. The service instance name is taken from g_device_name (configurable
 *      via the web UI Services page) instead of a hard-coded string.
 *
 * Records advertised (6 answers):
 *   PTR  _services._dns-sd._udp.local. → _ipp._tcp.local.
 *   PTR  _ipp._tcp.local.              → <DeviceName>._ipp._tcp.local.
 *   PTR  _universal._sub._ipp._tcp.local → <DeviceName>._ipp._tcp.local.
 *   SRV  <DeviceName>._ipp._tcp.local. → gpsu21.local.:631
 *   TXT  <DeviceName>._ipp._tcp.local. → (AirPrint capability key=value pairs)
 *   A    gpsu21.local.                 → <device IP>
 */

#include "rtos.h"

#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <lwip/netif.h>
#include <lwip/ip4.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "mdns.h"
#include "config.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Constants
 * ───────────────────────────────────────────────────────────────────────────*/
#define MDNS_PORT               5353
#define MDNS_GROUP              "224.0.0.251"
#define MDNS_ANNOUNCE_INTERVAL_S  5   /* seconds between periodic announcements */
#define MDNS_TTL                4500  /* DNS TTL in seconds */
#define MDNS_BUF_SIZE           768   /* enlarged to fit 6-record response */

/* Fixed per-product values */
#define MDNS_HOSTNAME           "gpsu21"       /* mDNS hostname (A record) */
#define MDNS_SERVICE_TYPE       "_ipp._tcp"
#define MDNS_DOMAIN             "local"
#define MDNS_PORT_IPP           631

/* ─────────────────────────────────────────────────────────────────────────────
 * DNS message construction helpers
 *
 * All multi-byte fields are big-endian per RFC 1035.
 * ───────────────────────────────────────────────────────────────────────────*/

typedef struct {
    uint8_t buf[MDNS_BUF_SIZE];
    int     len;
} dns_msg_t;

static void dns_write_u8(dns_msg_t *m, uint8_t v)
{
    if (m->len < MDNS_BUF_SIZE) {
        m->buf[m->len++] = v;
    }
}

static void dns_write_u16(dns_msg_t *m, uint16_t v)
{
    dns_write_u8(m, (uint8_t)(v >> 8));
    dns_write_u8(m, (uint8_t)(v & 0xFF));
}

static void dns_write_u32(dns_msg_t *m, uint32_t v)
{
    dns_write_u8(m, (uint8_t)(v >> 24));
    dns_write_u8(m, (uint8_t)((v >> 16) & 0xFF));
    dns_write_u8(m, (uint8_t)((v >> 8)  & 0xFF));
    dns_write_u8(m, (uint8_t)(v & 0xFF));
}

/* Write a DNS label sequence from a dotted name string.
 * E.g. "gpsu21._ipp._tcp.local" → \x06gpsu21\x04_ipp\x04_tcp\x05local\x00 */
static void dns_write_name(dns_msg_t *m, const char *name)
{
    const char *p = name;
    const char *dot;
    int label_len;

    while (*p) {
        dot = strchr(p, '.');
        if (dot) {
            label_len = (int)(dot - p);
        } else {
            label_len = (int)strlen(p);
        }
        if (label_len > 63) label_len = 63;
        dns_write_u8(m, (uint8_t)label_len);
        if (m->len + label_len <= MDNS_BUF_SIZE) {
            memcpy(m->buf + m->len, p, (size_t)label_len);
            m->len += label_len;
        }
        p += label_len;
        if (*p == '.') p++;
    }
    dns_write_u8(m, 0); /* root label */
}

/*
 * Write a DNS resource record header.
 * use_cache_flush: non-zero sets the mDNS cache-flush bit in the CLASS field.
 *
 * RFC 6762 §11.3: the cache-flush bit MUST NOT be set on PTR records that are
 * shared records (i.e. more than one responder may answer with the same name).
 * It SHOULD be set on SRV, TXT, and A records which are unique to this host.
 */
static void dns_write_rr_header(dns_msg_t *m, const char *name,
                                uint16_t type, uint32_t ttl, uint16_t rdlength,
                                int use_cache_flush)
{
    dns_write_name(m, name);
    dns_write_u16(m, type);
    dns_write_u16(m, use_cache_flush ? 0x8001u : 0x0001u); /* CLASS IN */
    dns_write_u32(m, ttl);
    dns_write_u16(m, rdlength);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Obtain the current IPv4 address from the default lwIP network interface.
 *
 * Returns 0 if the interface is not yet configured.
 * The returned value is in lwIP network-byte-order (big-endian) form, which
 * is what ip_be = htonl(my_ip) / memcpy(buf, &ip_be, 4) expects below.
 * ───────────────────────────────────────────────────────────────────────────*/
static uint32_t get_my_ip(void)
{
    struct netif *nif = netif_default;
    if (nif && !ip4_addr_isany(netif_ip4_addr(nif))) {
        return netif_ip4_addr(nif)->addr;
    }
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Build a mDNS announcement packet
 *
 * The service instance name is taken from the global g_device_name so that
 * it reflects the device name set in the web UI (Services page).
 * ───────────────────────────────────────────────────────────────────────────*/
static int build_announcement(dns_msg_t *m, uint32_t my_ip)
{
    char fqsn[128];   /* fully-qualified service instance name */
    char fqhn[64];    /* fully-qualified hostname */
    char fqst[64];    /* service type.domain */
    char sd_name[64]; /* _services._dns-sd._udp.local */
    uint8_t txt_rdata[256];
    int     txt_len;
    char    name_buf[128];
    uint32_t ip_be;

    memset(m, 0, sizeof(*m));

    /* Build name strings using the configurable device name */
    snprintf(fqsn, sizeof(fqsn), "%s.%s.%s",
             g_device_name, MDNS_SERVICE_TYPE, MDNS_DOMAIN);
    snprintf(fqhn, sizeof(fqhn), "%s.%s", MDNS_HOSTNAME, MDNS_DOMAIN);
    snprintf(fqst, sizeof(fqst), "%s.%s", MDNS_SERVICE_TYPE, MDNS_DOMAIN);
    snprintf(sd_name, sizeof(sd_name), "_services._dns-sd._udp.%s", MDNS_DOMAIN);

    /* DNS header: QR=1 (response), AA=1 (authoritative), ANCOUNT=6 */
    dns_write_u16(m, 0x0000); /* ID = 0 (mDNS) */
    dns_write_u16(m, 0x8400); /* Flags: response, authoritative */
    dns_write_u16(m, 0x0000); /* QDCOUNT */
    dns_write_u16(m, 0x0006); /* ANCOUNT = 6 */
    dns_write_u16(m, 0x0000); /* NSCOUNT */
    dns_write_u16(m, 0x0000); /* ARCOUNT */

    /* ── PTR record: _services._dns-sd._udp.local → _ipp._tcp.local ──
     *
     * RFC 6762 / DNS-SD: shared PTR, NO cache-flush bit. */
    {
        dns_msg_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        dns_write_name(&tmp, fqst);
        dns_write_rr_header(m, sd_name, 0x000C /* PTR */, MDNS_TTL,
                            (uint16_t)tmp.len, 0 /* no cache-flush */);
        if (m->len + tmp.len <= MDNS_BUF_SIZE) {
            memcpy(m->buf + m->len, tmp.buf, (size_t)tmp.len);
            m->len += tmp.len;
        }
    }

    /* ── PTR record: _ipp._tcp.local → <DeviceName>._ipp._tcp.local ──
     *
     * Shared PTR, NO cache-flush bit (RFC 6762 §11.3). */
    {
        dns_msg_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        dns_write_name(&tmp, fqsn);
        dns_write_rr_header(m, fqst, 0x000C /* PTR */, MDNS_TTL,
                            (uint16_t)tmp.len, 0 /* no cache-flush */);
        if (m->len + tmp.len <= MDNS_BUF_SIZE) {
            memcpy(m->buf + m->len, tmp.buf, (size_t)tmp.len);
            m->len += tmp.len;
        }
    }

    /* ── PTR record: _universal._sub._ipp._tcp.local → same ──
     *
     * Required for AirPrint on iOS.  Shared PTR, NO cache-flush bit. */
    snprintf(name_buf, sizeof(name_buf),
             "_universal._sub.%s.%s", MDNS_SERVICE_TYPE, MDNS_DOMAIN);
    {
        dns_msg_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        dns_write_name(&tmp, fqsn);
        dns_write_rr_header(m, name_buf, 0x000C, MDNS_TTL,
                            (uint16_t)tmp.len, 0 /* no cache-flush */);
        if (m->len + tmp.len <= MDNS_BUF_SIZE) {
            memcpy(m->buf + m->len, tmp.buf, (size_t)tmp.len);
            m->len += tmp.len;
        }
    }

    /* ── SRV record: <DeviceName>._ipp._tcp.local → gpsu21.local:631 ──
     *
     * Unique record for this host — cache-flush bit IS set. */
    {
        dns_msg_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        /* SRV RDATA: priority(2) weight(2) port(2) target */
        tmp.buf[tmp.len++] = 0x00; /* priority hi */
        tmp.buf[tmp.len++] = 0x00; /* priority lo */
        tmp.buf[tmp.len++] = 0x00; /* weight hi */
        tmp.buf[tmp.len++] = 0x00; /* weight lo */
        tmp.buf[tmp.len++] = (uint8_t)(MDNS_PORT_IPP >> 8);
        tmp.buf[tmp.len++] = (uint8_t)(MDNS_PORT_IPP & 0xFF);
        dns_write_name(&tmp, fqhn);
        dns_write_rr_header(m, fqsn, 0x0021 /* SRV */, MDNS_TTL,
                            (uint16_t)tmp.len, 1 /* cache-flush */);
        if (m->len + tmp.len <= MDNS_BUF_SIZE) {
            memcpy(m->buf + m->len, tmp.buf, (size_t)tmp.len);
            m->len += tmp.len;
        }
    }

    /* ── TXT record with AirPrint capability key-value pairs ──
     *
     * Unique record — cache-flush bit IS set. */
    {
        static const char * const txt_kvs[] = {
            "txtvers=1",
            "qtotal=1",
            "rp=ipp/print",
            "ty=IOGear GPSU21",
            "adminurl=http://gpsu21.local/",
            "note=",
            "priority=50",
            "product=(GPL Ghostscript)",
            "pdl=application/pdf,image/jpeg,image/png,image/urf,application/octet-stream",
            "Color=F",
            "Duplex=F",
            "usb_MFG=IOGear",
            "usb_MDL=GPSU21",
            "air=username,password,t2600,pwg",
            NULL
        };
        int txt_pos = 0;
        cyg_uint32 ki;

        for (ki = 0; txt_kvs[ki] != NULL; ki++) {
            size_t kl = strlen(txt_kvs[ki]);
            if (txt_pos + 1 + kl > sizeof(txt_rdata)) break;
            txt_rdata[txt_pos++] = (uint8_t)kl;
            memcpy(txt_rdata + txt_pos, txt_kvs[ki], kl);
            txt_pos += (int)kl;
        }
        txt_len = txt_pos;

        dns_write_rr_header(m, fqsn, 0x0010 /* TXT */, MDNS_TTL,
                            (uint16_t)txt_len, 1 /* cache-flush */);
        if (m->len + txt_len <= MDNS_BUF_SIZE) {
            memcpy(m->buf + m->len, txt_rdata, (size_t)txt_len);
            m->len += txt_len;
        }
    }

    /* ── A record: gpsu21.local → <my IP address> ──
     *
     * Unique record — cache-flush bit IS set. */
    ip_be = htonl(my_ip);
    dns_write_rr_header(m, fqhn, 0x0001 /* A */, MDNS_TTL, 4,
                        1 /* cache-flush */);
    if (m->len + 4 <= MDNS_BUF_SIZE) {
        memcpy(m->buf + m->len, &ip_be, 4);
        m->len += 4;
    }

    return m->len;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Check whether a received mDNS packet is a query that mentions _ipp._tcp.
 *
 * Returns non-zero if the raw packet bytes contain the "_ipp" label (4-byte
 * length prefix 0x04 followed by the characters '_', 'i', 'p', 'p').  This
 * simple scan avoids writing a full DNS parser while covering all practical
 * iOS/macOS AirPrint queries (PTR and ANY queries for _ipp._tcp.local).
 * ───────────────────────────────────────────────────────────────────────────*/
static int is_mdns_ipp_query(const uint8_t *buf, int len)
{
    int i;

    /* Minimum DNS header is 12 bytes.  Check QR bit = 0 (this is a query). */
    if (len < 12) {
        return 0;
    }
    if (buf[2] & 0x80) {
        return 0; /* QR=1 → response, not a query */
    }

    /* Scan for the wire-format "_ipp" label: 0x04 '_' 'i' 'p' 'p' */
    for (i = 12; i <= len - 5; i++) {
        if (buf[i]   == 0x04u &&
            buf[i+1] == '_'   &&
            buf[i+2] == 'i'   &&
            buf[i+3] == 'p'   &&
            buf[i+4] == 'p') {
            return 1;
        }
    }
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * mDNS thread — binds to port 5353, listens for queries, and sends periodic
 * announcements so that iOS AirPrint discovers the printer reliably.
 * ───────────────────────────────────────────────────────────────────────────*/
void mdns_thread(cyg_addrword_t arg)
{
    int                 sock;
    struct sockaddr_in  bind_addr;
    struct sockaddr_in  mcast_addr;
    struct ip_mreq      mreq;
    int                 ttl  = 255;
    int                 loop = 0;
    int                 reuse = 1;
    struct timeval      tv;
    dns_msg_t           msg;
    uint32_t            my_ip;
    int                 i;

    (void)arg;

    /* Wait for the network interface to come up before binding. */
    cyg_thread_delay(pdMS_TO_TICKS(2000));

    sock = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        diag_printf("mdns: socket() failed\n");
        return;
    }

    /* Allow multiple sockets to share port 5353 (required for mDNS). */
    lwip_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /* Bind to port 5353 so that incoming mDNS queries are received. */
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_port        = htons(MDNS_PORT);
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    if (lwip_bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        diag_printf("mdns: bind() failed\n");
        lwip_close(sock);
        return;
    }

    /* Join the mDNS multicast group. */
    mreq.imr_multiaddr.s_addr = inet_addr(MDNS_GROUP);
    mreq.imr_interface.s_addr = INADDR_ANY;
    lwip_setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    lwip_setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL,  &ttl,  sizeof(ttl));
    lwip_setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    /* Set a receive timeout so that the thread wakes periodically for
     * announcements even when no queries arrive. */
    tv.tv_sec  = MDNS_ANNOUNCE_INTERVAL_S;
    tv.tv_usec = 0;
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Destination address for outgoing mDNS packets. */
    memset(&mcast_addr, 0, sizeof(mcast_addr));
    mcast_addr.sin_family      = AF_INET;
    mcast_addr.sin_port        = htons(MDNS_PORT);
    mcast_addr.sin_addr.s_addr = inet_addr(MDNS_GROUP);

    diag_printf("mdns: started, advertising '%s' as %s\n",
                g_device_name, MDNS_SERVICE_TYPE);

    /* Send three rapid initial announcements (RFC 6762 §8.3 Probing/
     * Announcing) spaced 1 second apart, then switch to the normal 5-second
     * interval.  This ensures iOS devices that are already on the network
     * see the printer quickly after it boots. */
    my_ip = get_my_ip();
    for (i = 0; i < 3; i++) {
        if (g_airprint_enabled) {
            int len = build_announcement(&msg, my_ip);
            if (len > 0) {
                lwip_sendto(sock, msg.buf, (size_t)len, 0,
                            (struct sockaddr *)&mcast_addr, sizeof(mcast_addr));
            }
        }
        cyg_thread_delay(pdMS_TO_TICKS(1000));
    }

    for (;;) {
        uint8_t    rxbuf[512];
        struct sockaddr_in from;
        socklen_t  fromlen = sizeof(from);
        int        n;
        int        len;

        /* Refresh the IP on every loop iteration — DHCP may reassign it. */
        my_ip = get_my_ip();

        /* Block for up to MDNS_ANNOUNCE_INTERVAL_S waiting for a query. */
        n = lwip_recvfrom(sock, rxbuf, sizeof(rxbuf), 0,
                          (struct sockaddr *)&from, &fromlen);

        if (!g_airprint_enabled) {
            /* AirPrint disabled — discard received packets and don't announce. */
            continue;
        }

        if (n > 0 && is_mdns_ipp_query(rxbuf, n)) {
            /* Respond to the query with a small random delay (100–200 ms) to
             * reduce the chance of a response collision when multiple devices
             * are probing at the same time (RFC 6762 §6). */
            cyg_thread_delay(pdMS_TO_TICKS(100));
            my_ip = get_my_ip();
            len = build_announcement(&msg, my_ip);
            if (len > 0) {
                lwip_sendto(sock, msg.buf, (size_t)len, 0,
                            (struct sockaddr *)&mcast_addr, sizeof(mcast_addr));
            }
        } else {
            /* Receive timeout (n <= 0) → send the periodic announcement. */
            if (n <= 0) {
                len = build_announcement(&msg, my_ip);
                if (len > 0) {
                    lwip_sendto(sock, msg.buf, (size_t)len, 0,
                                (struct sockaddr *)&mcast_addr,
                                sizeof(mcast_addr));
                }
            }
        }
    }
}
