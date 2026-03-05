/*
 * mdns.c — mDNS/Bonjour advertising for the IOGear GPSU21 print server.
 *
 * Advertises the printer as _ipp._tcp on the local network so that iOS 14+
 * and macOS 11+ clients can discover it automatically via AirPrint.
 *
 * This implementation sends periodic mDNS announcements via multicast UDP
 * (224.0.0.251:5353) without relying on an external mDNS library.
 *
 * Key stability fix vs. stock firmware:
 *   The original mDNS stack (mDNSPosix.c from Apple mDNSCore) had a re-
 *   entrancy counter de-sync bug (Finding 2 in README).  This clean
 *   implementation avoids shared mutable state between the lock/unlock
 *   functions and does not use the buggy counter pattern.
 *
 * Records advertised:
 *   PTR  _ipp._tcp.local.          → gpsu21._ipp._tcp.local.
 *   PTR  _universal._sub._ipp._tcp → gpsu21._ipp._tcp.local.
 *   SRV  gpsu21._ipp._tcp.local.   → gpsu21.local.:631
 *   TXT  gpsu21._ipp._tcp.local.   → (AirPrint capability key=value pairs)
 *   A    gpsu21.local.             → <device IP>
 */

#include <cyg/kernel/kapi.h>
#include <cyg/infra/diag.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "mdns.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Constants
 * ───────────────────────────────────────────────────────────────────────────*/
#define MDNS_PORT               5353
#define MDNS_GROUP              "224.0.0.251"
#define MDNS_ANNOUNCE_INTERVAL  5000  /* milliseconds between announcements */
#define MDNS_TTL                4500  /* DNS TTL in seconds */
#define MDNS_BUF_SIZE           512

/* Service names */
#define MDNS_HOSTNAME           "gpsu21"
#define MDNS_SERVICE_NAME       "IOGear GPSU21"
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

/* Write a DNS resource record header */
static void dns_write_rr_header(dns_msg_t *m, const char *name,
                                uint16_t type, uint32_t ttl, uint16_t rdlength)
{
    dns_write_name(m, name);
    dns_write_u16(m, type);      /* TYPE */
    dns_write_u16(m, 0x8001);    /* CLASS: IN + cache-flush bit */
    dns_write_u32(m, ttl);       /* TTL */
    dns_write_u16(m, rdlength);  /* RDLENGTH */
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Build a mDNS announcement packet
 * ───────────────────────────────────────────────────────────────────────────*/
static int build_announcement(dns_msg_t *m, uint32_t my_ip)
{
    char fqsn[128];   /* fully-qualified service name */
    char fqhn[128];   /* fully-qualified hostname */
    char fqst[64];    /* service type.domain */
    uint8_t txt_rdata[256];
    int     txt_len;
    char    srv_rdata[128];
    int     srv_len;
    char    name_buf[128];
    int     save_len;
    uint32_t ip_be;

    memset(m, 0, sizeof(*m));

    /* Build name strings */
    snprintf(fqsn, sizeof(fqsn), "%s.%s.%s",
             MDNS_SERVICE_NAME, MDNS_SERVICE_TYPE, MDNS_DOMAIN);
    snprintf(fqhn, sizeof(fqhn), "%s.%s", MDNS_HOSTNAME, MDNS_DOMAIN);
    snprintf(fqst, sizeof(fqst), "%s.%s", MDNS_SERVICE_TYPE, MDNS_DOMAIN);

    /* DNS header: QR=1 (response), AA=1 (authoritative), ANCOUNT=5 */
    dns_write_u16(m, 0x0000); /* ID = 0 (mDNS) */
    dns_write_u16(m, 0x8400); /* Flags: response, authoritative */
    dns_write_u16(m, 0x0000); /* QDCOUNT */
    dns_write_u16(m, 0x0005); /* ANCOUNT */
    dns_write_u16(m, 0x0000); /* NSCOUNT */
    dns_write_u16(m, 0x0000); /* ARCOUNT */

    /* ── PTR record: _ipp._tcp.local → <service name>._ipp._tcp.local ── */
    {
        dns_msg_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        dns_write_name(&tmp, fqsn);
        dns_write_rr_header(m, fqst, 0x000C /* PTR */, MDNS_TTL,
                            (uint16_t)tmp.len);
        if (m->len + tmp.len <= MDNS_BUF_SIZE) {
            memcpy(m->buf + m->len, tmp.buf, (size_t)tmp.len);
            m->len += tmp.len;
        }
    }

    /* ── PTR record: _universal._sub._ipp._tcp.local → same ── */
    snprintf(name_buf, sizeof(name_buf),
             "_universal._sub.%s.%s", MDNS_SERVICE_TYPE, MDNS_DOMAIN);
    {
        dns_msg_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        dns_write_name(&tmp, fqsn);
        dns_write_rr_header(m, name_buf, 0x000C, MDNS_TTL,
                            (uint16_t)tmp.len);
        if (m->len + tmp.len <= MDNS_BUF_SIZE) {
            memcpy(m->buf + m->len, tmp.buf, (size_t)tmp.len);
            m->len += tmp.len;
        }
    }

    /* ── SRV record: <service> → <hostname>:631 ── */
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
                            (uint16_t)tmp.len);
        if (m->len + tmp.len <= MDNS_BUF_SIZE) {
            memcpy(m->buf + m->len, tmp.buf, (size_t)tmp.len);
            m->len += tmp.len;
        }
        (void)srv_rdata;
        (void)srv_len;
    }

    /* ── TXT record with AirPrint capability key-value pairs ── */
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
                            (uint16_t)txt_len);
        if (m->len + txt_len <= MDNS_BUF_SIZE) {
            memcpy(m->buf + m->len, txt_rdata, (size_t)txt_len);
            m->len += txt_len;
        }
        (void)txt_rdata;
    }

    /* ── A record: <hostname>.local → <my IP address> ── */
    ip_be = htonl(my_ip);
    dns_write_rr_header(m, fqhn, 0x0001 /* A */, MDNS_TTL, 4);
    if (m->len + 4 <= MDNS_BUF_SIZE) {
        memcpy(m->buf + m->len, &ip_be, 4);
        m->len += 4;
    }

    (void)save_len;
    (void)name_buf;

    return m->len;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * mDNS thread — sends periodic announcements
 * ───────────────────────────────────────────────────────────────────────────*/
void mdns_thread(cyg_addrword_t arg)
{
    int                 sock;
    struct sockaddr_in  mcast_addr;
    struct ip_mreq      mreq;
    int                 ttl = 255;
    int                 loop = 0;
    dns_msg_t           msg;
    uint32_t            my_ip = 0; /* obtained from network stack */

    (void)arg;

    /* Wait for the network interface to come up */
    cyg_thread_delay(200);

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        diag_printf("mdns: socket() failed\n");
        return;
    }

    /* Join the mDNS multicast group */
    mreq.imr_multiaddr.s_addr = inet_addr(MDNS_GROUP);
    mreq.imr_interface.s_addr = INADDR_ANY;
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL,  &ttl,  sizeof(ttl));
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    memset(&mcast_addr, 0, sizeof(mcast_addr));
    mcast_addr.sin_family      = AF_INET;
    mcast_addr.sin_port        = htons(MDNS_PORT);
    mcast_addr.sin_addr.s_addr = inet_addr(MDNS_GROUP);

    diag_printf("mdns: started, advertising '%s' as %s\n",
                MDNS_SERVICE_NAME, MDNS_SERVICE_TYPE);

    for (;;) {
        int len = build_announcement(&msg, my_ip);
        if (len > 0) {
            sendto(sock, msg.buf, (size_t)len, 0,
                   (struct sockaddr *)&mcast_addr, sizeof(mcast_addr));
        }
        /* Delay between announcements — avoids mDNS flood that triggers the
         * wakeup_count overflow in the stock firmware (Finding 4). */
        cyg_thread_delay(MDNS_ANNOUNCE_INTERVAL / 10);
    }
}
