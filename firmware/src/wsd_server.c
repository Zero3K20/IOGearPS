/*
 * wsd_server.c — WS-Discovery + WSD-Scan server for Windows scanner support.
 *
 * Windows uses two protocols to discover and communicate with network scanners:
 *
 *   1. WS-Discovery (UDP, multicast 239.255.255.250:3702)
 *      Windows broadcasts Probe messages to find devices that advertise
 *      scan:ScannerServiceType.  This thread joins the WSD multicast group,
 *      sends a Hello when the device boots, and replies to Probes with a
 *      ProbeMatch pointing to the WSD-Scan HTTP endpoint.
 *
 *   2. WSD-Scan HTTP server (TCP port 5357)
 *      After discovery, Windows fetches device metadata (WS-Transfer GET) and
 *      then sends SOAP POST requests to perform scanner operations.
 *      Handled operations:
 *        WS-Transfer Get          — device and scanner metadata
 *        GetScannerElements       — scanner capabilities (modes, resolutions)
 *        CreateScanJob            — accept a new scan job (stubbed)
 *        GetJobElements           — scan job status
 *        RetrieveImage            — scanned image data (returns fault;
 *                                   USB scanner driver is TODO)
 *
 * The eSCL server (escl_server.c, port 9290) serves iOS/macOS AirScan
 * clients using Apple's HTTP-based eSCL protocol; this file serves Windows
 * clients using Microsoft's SOAP-based WSD-Scan protocol.  Both protocols
 * are controlled by the g_scanner_enabled configuration flag.
 *
 * References:
 *   WS-Discovery: http://schemas.xmlsoap.org/ws/2005/04/discovery
 *   WSD Device Profile: http://schemas.xmlsoap.org/ws/2006/02/devprof
 *   WSD-Scan: http://schemas.microsoft.com/windows/2006/08/wdp/scan
 */

#include "rtos.h"

#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <lwip/netif.h>
#include <lwip/ip4.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include "wsd_server.h"
#include "config.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Constants
 * ───────────────────────────────────────────────────────────────────────────*/

/* WS-Discovery multicast */
#define WSD_MCAST_GROUP         "239.255.255.250"
#define WSD_DISC_PORT           3702

/* WSD-Scan HTTP */
#define WSD_HTTP_PORT           5357
#define WSD_HTTP_MAX_CONN       4
#define WSD_HTTP_STACK_SIZE     4096
#define WSD_HTTP_PRIORITY       12

/* Device/service identifiers */
#define WSD_DEVICE_UUID         "00000000-0000-0000-0000-000000001243"
#define WSD_SCANNER_SVC_UUID    "00000000-0000-0000-0001-000000001243"
#define WSD_DEVICE_NAME         "IOGear GPSU21"
#define WSD_MANUFACTURER        "IOGear"
#define WSD_MODEL_NAME          "GPSU21"
#define WSD_FIRMWARE_VER        "9.09.56"
#define WSD_SERIAL              "00001243"

/* ─────────────────────────────────────────────────────────────────────────────
 * SOAP/WSD XML string templates
 *
 * All SOAP templates use sequential %s format specifiers:
 *   1st %s — outgoing MessageID UUID for this response
 *   2nd %s — RelatesTo (incoming request MessageID, echoed back)
 *   3rd %s — device IP address string, where the template needs it
 *             (only WSD_PROBEMATCH_TMPL and WSD_METADATA_TMPL use 3 args;
 *              WSD_HELLO_TMPL uses only 2: msgid and ip_str)
 * ───────────────────────────────────────────────────────────────────────────*/

/* Common SOAP envelope open — declares all namespace prefixes used. */
#define WSD_NS \
    "xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" " \
    "xmlns:a=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" " \
    "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" " \
    "xmlns:p=\"http://schemas.xmlsoap.org/ws/2006/02/devprof\" " \
    "xmlns:scan=\"http://schemas.microsoft.com/windows/2006/08/wdp/scan\" " \
    "xmlns:mex=\"http://schemas.xmlsoap.org/ws/2004/09/mex\""

/* ── WS-Discovery: Hello ────────────────────────────────────────────────────
 * Sent as UDP multicast on boot.  snprintf args: msgid, ip_str */
static const char WSD_HELLO_TMPL[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<s:Envelope " WSD_NS ">"
    "<s:Header>"
    "<a:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</a:To>"
    "<a:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Hello</a:Action>"
    "<a:MessageID>urn:uuid:%s</a:MessageID>"
    "<d:AppSequence InstanceId=\"1\""
    " SequenceId=\"urn:uuid:00000000-0000-0000-ffff-000000000001\""
    " MessageNumber=\"1\"/>"
    "</s:Header>"
    "<s:Body>"
    "<d:Hello>"
    "<a:EndpointReference>"
    "<a:Address>urn:uuid:" WSD_DEVICE_UUID "</a:Address>"
    "</a:EndpointReference>"
    "<d:Types>p:Device scan:ScannerServiceType</d:Types>"
    "<d:Scopes>ldap:///</d:Scopes>"
    "<d:XAddrs>http://%s:5357/</d:XAddrs>"
    "<d:MetadataVersion>1</d:MetadataVersion>"
    "</d:Hello>"
    "</s:Body>"
    "</s:Envelope>";

/* ── WS-Discovery: ProbeMatch ───────────────────────────────────────────────
 * Sent as UDP unicast to the Probe sender.
 * snprintf args: msgid, relatesTo, ip_str */
static const char WSD_PROBEMATCH_TMPL[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<s:Envelope " WSD_NS ">"
    "<s:Header>"
    "<a:To>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</a:To>"
    "<a:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/ProbeMatches</a:Action>"
    "<a:MessageID>urn:uuid:%s</a:MessageID>"
    "<a:RelatesTo>%s</a:RelatesTo>"
    "</s:Header>"
    "<s:Body>"
    "<d:ProbeMatches>"
    "<d:ProbeMatch>"
    "<a:EndpointReference>"
    "<a:Address>urn:uuid:" WSD_DEVICE_UUID "</a:Address>"
    "</a:EndpointReference>"
    "<d:Types>p:Device scan:ScannerServiceType</d:Types>"
    "<d:Scopes>ldap:///</d:Scopes>"
    "<d:XAddrs>http://%s:5357/</d:XAddrs>"
    "<d:MetadataVersion>1</d:MetadataVersion>"
    "</d:ProbeMatch>"
    "</d:ProbeMatches>"
    "</s:Body>"
    "</s:Envelope>";

/* ── WSD HTTP: WS-Transfer GetResponse (device metadata) ───────────────────
 * snprintf args: msgid, relatesTo, ip_str (for ScannerService endpoint) */
static const char WSD_METADATA_TMPL[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<s:Envelope " WSD_NS ">"
    "<s:Header>"
    "<a:To>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</a:To>"
    "<a:Action>http://schemas.xmlsoap.org/ws/2004/09/transfer/GetResponse</a:Action>"
    "<a:MessageID>urn:uuid:%s</a:MessageID>"
    "<a:RelatesTo>%s</a:RelatesTo>"
    "</s:Header>"
    "<s:Body>"
    "<mex:Metadata>"
    "<mex:MetadataSection"
    " Dialect=\"http://schemas.xmlsoap.org/ws/2006/02/devprof/ThisDevice\">"
    "<p:ThisDevice>"
    "<p:FriendlyName>" WSD_DEVICE_NAME "</p:FriendlyName>"
    "<p:FirmwareVersion>" WSD_FIRMWARE_VER "</p:FirmwareVersion>"
    "<p:SerialNumber>" WSD_SERIAL "</p:SerialNumber>"
    "</p:ThisDevice>"
    "</mex:MetadataSection>"
    "<mex:MetadataSection"
    " Dialect=\"http://schemas.xmlsoap.org/ws/2006/02/devprof/ThisModel\">"
    "<p:ThisModel>"
    "<p:Manufacturer>" WSD_MANUFACTURER "</p:Manufacturer>"
    "<p:ManufacturerUrl>http://www.iogear.com/</p:ManufacturerUrl>"
    "<p:ModelName>" WSD_MODEL_NAME "</p:ModelName>"
    "<p:ModelUrl>http://www.iogear.com/</p:ModelUrl>"
    "<p:PresentationUrl>http://gpsu21.local/</p:PresentationUrl>"
    "</p:ThisModel>"
    "</mex:MetadataSection>"
    "<mex:MetadataSection"
    " Dialect=\"http://schemas.xmlsoap.org/ws/2006/02/devprof/Relationship\">"
    "<p:Relationship"
    " Type=\"http://schemas.xmlsoap.org/ws/2006/02/devprof/host\">"
    "<p:Hosted>"
    "<a:EndpointReference>"
    "<a:Address>http://%s:5357/ScannerService</a:Address>"
    "</a:EndpointReference>"
    "<p:Types>scan:ScannerServiceType</p:Types>"
    "<p:ServiceId>urn:uuid:" WSD_SCANNER_SVC_UUID "</p:ServiceId>"
    "</p:Hosted>"
    "</p:Relationship>"
    "</mex:MetadataSection>"
    "</mex:Metadata>"
    "</s:Body>"
    "</s:Envelope>";

/* ── WSD-Scan: GetScannerElementsResponse ───────────────────────────────────
 * snprintf args: msgid, relatesTo */
static const char WSD_SCANNER_ELEM_TMPL[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<s:Envelope " WSD_NS ">"
    "<s:Header>"
    "<a:To>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</a:To>"
    "<a:Action>"
    "http://schemas.microsoft.com/windows/2006/08/wdp/scan/GetScannerElementsResponse"
    "</a:Action>"
    "<a:MessageID>urn:uuid:%s</a:MessageID>"
    "<a:RelatesTo>%s</a:RelatesTo>"
    "</s:Header>"
    "<s:Body>"
    "<scan:GetScannerElementsResponse>"
    "<scan:ScannerElements>"
    "<scan:ElementData Name=\"scan:ScannerDescription\" Valid=\"true\">"
    "<scan:ScannerDescription>"
    "<scan:ScannerName>" WSD_DEVICE_NAME "</scan:ScannerName>"
    "<scan:ScannerInfo>" WSD_DEVICE_NAME " USB Scanner Server</scan:ScannerInfo>"
    "<scan:ScannerLocation></scan:ScannerLocation>"
    "</scan:ScannerDescription>"
    "</scan:ElementData>"
    "<scan:ElementData Name=\"scan:ScannerCapabilities\" Valid=\"true\">"
    "<scan:ScannerCapabilities>"
    "<scan:PlatenCapabilities>"
    "<scan:PlatenColor>"
    "<scan:ColorEntry>BlackAndWhite1</scan:ColorEntry>"
    "<scan:ColorEntry>Grayscale8</scan:ColorEntry>"
    "<scan:ColorEntry>RGB24</scan:ColorEntry>"
    "</scan:PlatenColor>"
    "<scan:PlatenMinimumSize>"
    "<scan:Width>50</scan:Width>"
    "<scan:Height>50</scan:Height>"
    "</scan:PlatenMinimumSize>"
    "<scan:PlatenMaximumSize>"
    "<scan:Width>2550</scan:Width>"
    "<scan:Height>3508</scan:Height>"
    "</scan:PlatenMaximumSize>"
    "<scan:PlatenResolutions>"
    "<scan:Widths>"
    "<scan:Width>75</scan:Width>"
    "<scan:Width>150</scan:Width>"
    "<scan:Width>300</scan:Width>"
    "</scan:Widths>"
    "<scan:Heights>"
    "<scan:Height>75</scan:Height>"
    "<scan:Height>150</scan:Height>"
    "<scan:Height>300</scan:Height>"
    "</scan:Heights>"
    "</scan:PlatenResolutions>"
    "<scan:PlatenFormats>"
    "<scan:FormatValue>jfif</scan:FormatValue>"
    "<scan:FormatValue>pdf-a</scan:FormatValue>"
    "<scan:FormatValue>png</scan:FormatValue>"
    "</scan:PlatenFormats>"
    "</scan:PlatenCapabilities>"
    "</scan:ScannerCapabilities>"
    "</scan:ElementData>"
    "<scan:ElementData Name=\"scan:ScannerStatus\" Valid=\"true\">"
    "<scan:ScannerStatus>"
    "<scan:ScannerState>Idle</scan:ScannerState>"
    "<scan:ScannerStateReasons/>"
    "<scan:ActiveConditions/>"
    "<scan:ConditionHistory/>"
    "<scan:Jobs/>"
    "</scan:ScannerStatus>"
    "</scan:ElementData>"
    "</scan:ScannerElements>"
    "<scan:ScannerElementsChangeNumber>1</scan:ScannerElementsChangeNumber>"
    "</scan:GetScannerElementsResponse>"
    "</s:Body>"
    "</s:Envelope>";

/* ── WSD-Scan: CreateScanJobResponse ────────────────────────────────────────
 * snprintf args: msgid, relatesTo */
static const char WSD_CREATE_JOB_TMPL[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<s:Envelope " WSD_NS ">"
    "<s:Header>"
    "<a:To>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</a:To>"
    "<a:Action>"
    "http://schemas.microsoft.com/windows/2006/08/wdp/scan/CreateScanJobResponse"
    "</a:Action>"
    "<a:MessageID>urn:uuid:%s</a:MessageID>"
    "<a:RelatesTo>%s</a:RelatesTo>"
    "</s:Header>"
    "<s:Body>"
    "<scan:CreateScanJobResponse>"
    "<scan:JobId>1</scan:JobId>"
    "<scan:JobToken>T1</scan:JobToken>"
    "<scan:DocumentFinalParameters>"
    "<scan:Format>jfif</scan:Format>"
    "<scan:CompressionQualityFactor>0</scan:CompressionQualityFactor>"
    "<scan:ImagingBBox>"
    "<scan:Left>0</scan:Left>"
    "<scan:Top>0</scan:Top>"
    "<scan:Width>2550</scan:Width>"
    "<scan:Height>3508</scan:Height>"
    "</scan:ImagingBBox>"
    "<scan:InputSource>Platen</scan:InputSource>"
    "<scan:ColorProcessing>RGB24</scan:ColorProcessing>"
    "<scan:Resolution>"
    "<scan:Width>300</scan:Width>"
    "<scan:Height>300</scan:Height>"
    "</scan:Resolution>"
    "</scan:DocumentFinalParameters>"
    "<scan:ImageInformation>"
    "<scan:PixelsPerLine>2550</scan:PixelsPerLine>"
    "<scan:NumberOfLines>3508</scan:NumberOfLines>"
    "<scan:BytesPerLine>7650</scan:BytesPerLine>"
    "</scan:ImageInformation>"
    "<scan:ScannerElementsChangeNumber>1</scan:ScannerElementsChangeNumber>"
    "</scan:CreateScanJobResponse>"
    "</s:Body>"
    "</s:Envelope>";

/* ── WSD-Scan: GetJobElementsResponse ───────────────────────────────────────
 * snprintf args: msgid, relatesTo */
static const char WSD_GET_JOB_TMPL[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<s:Envelope " WSD_NS ">"
    "<s:Header>"
    "<a:To>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</a:To>"
    "<a:Action>"
    "http://schemas.microsoft.com/windows/2006/08/wdp/scan/GetJobElementsResponse"
    "</a:Action>"
    "<a:MessageID>urn:uuid:%s</a:MessageID>"
    "<a:RelatesTo>%s</a:RelatesTo>"
    "</s:Header>"
    "<s:Body>"
    "<scan:GetJobElementsResponse>"
    "<scan:JobElements>"
    "<scan:ElementData Name=\"scan:JobStatus\" Valid=\"true\">"
    "<scan:JobStatus>"
    "<scan:JobId>1</scan:JobId>"
    "<scan:JobToken>T1</scan:JobToken>"
    "<scan:JobState>Pending</scan:JobState>"
    "<scan:JobStateReasons>"
    "<scan:JobStateReason>JobWaitingForIntervention</scan:JobStateReason>"
    "</scan:JobStateReasons>"
    "<scan:ScansCompleted>0</scan:ScansCompleted>"
    "</scan:JobStatus>"
    "</scan:ElementData>"
    "</scan:JobElements>"
    "</scan:GetJobElementsResponse>"
    "</s:Body>"
    "</s:Envelope>";

/* ── SOAP Fault — returned for RetrieveImage (USB scanner not yet implemented)
 * snprintf args: msgid, relatesTo */
static const char WSD_FAULT_TMPL[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<s:Envelope " WSD_NS ">"
    "<s:Header>"
    "<a:To>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</a:To>"
    "<a:Action>http://www.w3.org/2003/05/soap-envelope/fault</a:Action>"
    "<a:MessageID>urn:uuid:%s</a:MessageID>"
    "<a:RelatesTo>%s</a:RelatesTo>"
    "</s:Header>"
    "<s:Body>"
    "<s:Fault>"
    "<s:Code><s:Value>s:Sender</s:Value></s:Code>"
    "<s:Reason>"
    "<s:Text xml:lang=\"en\">Scanner not ready — USB scanner driver not yet"
    " implemented.</s:Text>"
    "</s:Reason>"
    "</s:Fault>"
    "</s:Body>"
    "</s:Envelope>";

/* ─────────────────────────────────────────────────────────────────────────────
 * Utility helpers
 * ───────────────────────────────────────────────────────────────────────────*/

/* Monotonic sequence counter used to generate unique-enough WSD Message IDs. */
static uint32_t wsd_msg_seq = 0;

/*
 * Format a WSD MessageID UUID string into buf.
 * We use the fixed pattern urn:uuid:00000000-0000-0000-ffff-NNNNNNNNNNNN
 * where N is a decimal counter — sufficient for a single-device implementation.
 */
static void wsd_format_msgid(char *buf, size_t sz)
{
    snprintf(buf, sz, "00000000-0000-0000-ffff-%012u",
             (unsigned)wsd_msg_seq++);
}

/*
 * Get the current device IPv4 address as a dotted-decimal string.
 * Returns "0.0.0.0" if the interface is not yet configured.
 */
static void wsd_get_ip_str(char *buf, size_t sz)
{
    struct netif *nif = netif_default;
    if (nif && !ip4_addr_isany(netif_ip4_addr(nif))) {
        const uint8_t *b = (const uint8_t *)&netif_ip4_addr(nif)->addr;
        snprintf(buf, sz, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    } else {
        strncpy(buf, "0.0.0.0", sz);
        buf[sz - 1] = '\0';
    }
}

/*
 * Search for needle (as a C string) inside [buf, buf+len).
 * Returns non-zero if found.  Avoids relying on memmem() availability.
 */
static int wsd_contains(const char *buf, int len, const char *needle)
{
    int nlen = (int)strlen(needle);
    int i;

    if (nlen > len) {
        return 0;
    }
    for (i = 0; i <= len - nlen; i++) {
        if (memcmp(buf + i, needle, (size_t)nlen) == 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * Extract the MessageID value from a SOAP envelope in buf[0..len-1].
 * Searches for "MessageID>" (any namespace prefix) and copies the content
 * up to the next "</" into out[0..sz-1].
 * Falls back to "urn:uuid:unknown" if not found.
 */
static void wsd_extract_msgid(const char *buf, int len,
                               char *out, size_t sz)
{
    const char *p;
    const char *end;

    /* strstr-safe: buf is a NUL-terminated string built in the read loop. */
    (void)len;
    p = strstr(buf, "MessageID>");
    if (p) {
        p += 10; /* skip "MessageID>" */
        end = strstr(p, "</");
        if (end && (size_t)(end - p) < sz) {
            strncpy(out, p, (size_t)(end - p));
            out[end - p] = '\0';
            return;
        }
    }
    strncpy(out, "urn:uuid:unknown", sz);
    out[sz - 1] = '\0';
}

/* ─────────────────────────────────────────────────────────────────────────────
 * WS-Discovery thread — UDP port 3702
 *
 * Joins the WSD multicast group, sends a Hello on startup, then loops
 * responding to Probe messages that request scan:ScannerServiceType.
 * ───────────────────────────────────────────────────────────────────────────*/
void wsd_discovery_thread(cyg_addrword_t arg)
{
    int                sock;
    struct sockaddr_in bind_addr;
    struct sockaddr_in mcast_addr;
    struct ip_mreq     mreq;
    int                reuse = 1;
    int                ttl   = 255;
    int                loop  = 0;
    struct timeval     tv;
    char               ip_str[20];
    char               msgid[64];
    char              *sndbuf;
    char               rcvbuf[2048];
    int                n;
    struct sockaddr_in from;
    socklen_t          fromlen;
    char               relatesTo[256];

    (void)arg;

    /* Wait for the network to come up and for g_scanner_enabled to be stable. */
    cyg_thread_delay(pdMS_TO_TICKS(3000));

    if (!g_scanner_enabled) {
        diag_printf("wsd_disc: scanner disabled — thread exiting\n");
        return;
    }

    sndbuf = (char *)malloc(2048);
    if (!sndbuf) {
        diag_printf("wsd_disc: malloc failed\n");
        return;
    }

    sock = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        diag_printf("wsd_disc: socket() failed\n");
        free(sndbuf);
        return;
    }

    lwip_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_port        = htons(WSD_DISC_PORT);
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    if (lwip_bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        diag_printf("wsd_disc: bind() failed\n");
        lwip_close(sock);
        free(sndbuf);
        return;
    }

    /* Join WSD multicast group. */
    mreq.imr_multiaddr.s_addr = inet_addr(WSD_MCAST_GROUP);
    mreq.imr_interface.s_addr = INADDR_ANY;
    lwip_setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    lwip_setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL,  &ttl,  sizeof(ttl));
    lwip_setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    /* Set receive timeout so the thread wakes periodically. */
    tv.tv_sec  = 30;
    tv.tv_usec = 0;
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Multicast destination address. */
    memset(&mcast_addr, 0, sizeof(mcast_addr));
    mcast_addr.sin_family      = AF_INET;
    mcast_addr.sin_port        = htons(WSD_DISC_PORT);
    mcast_addr.sin_addr.s_addr = inet_addr(WSD_MCAST_GROUP);

    wsd_get_ip_str(ip_str, sizeof(ip_str));
    diag_printf("wsd_disc: started, IP=%s\n", ip_str);

    /* Send Hello to announce the scanner on the network. */
    wsd_format_msgid(msgid, sizeof(msgid));
    snprintf(sndbuf, 2048, WSD_HELLO_TMPL, msgid, ip_str);
    lwip_sendto(sock, sndbuf, strlen(sndbuf), 0,
                (struct sockaddr *)&mcast_addr, sizeof(mcast_addr));

    for (;;) {
        if (!g_scanner_enabled) {
            cyg_thread_delay(pdMS_TO_TICKS(1000));
            continue;
        }

        fromlen = sizeof(from);
        n = lwip_recvfrom(sock, rcvbuf, sizeof(rcvbuf) - 1, 0,
                          (struct sockaddr *)&from, &fromlen);
        if (n <= 0) {
            /* Receive timeout — send periodic Hello to re-announce. */
            wsd_get_ip_str(ip_str, sizeof(ip_str));
            wsd_format_msgid(msgid, sizeof(msgid));
            snprintf(sndbuf, 2048, WSD_HELLO_TMPL, msgid, ip_str);
            lwip_sendto(sock, sndbuf, strlen(sndbuf), 0,
                        (struct sockaddr *)&mcast_addr, sizeof(mcast_addr));
            continue;
        }
        rcvbuf[n] = '\0';

        /* Check if the message is a WSD Probe (QR=not set for queries).
         * We respond to any Probe that mentions ScannerServiceType or
         * that has no explicit types restriction (empty Types = match-all). */
        if (!wsd_contains(rcvbuf, n, "Probe") ||
             wsd_contains(rcvbuf, n, "ProbeMatches")) {
            continue; /* not a Probe, or it is a ProbeMatches response */
        }

        /* Only respond to probes requesting scanner types or untyped probes. */
        if (wsd_contains(rcvbuf, n, "Types") &&
            !wsd_contains(rcvbuf, n, "ScannerServiceType") &&
            !wsd_contains(rcvbuf, n, "wsdp:Device")) {
            continue;
        }

        /* Extract the incoming MessageID to use as RelatesTo. */
        wsd_extract_msgid(rcvbuf, n, relatesTo, sizeof(relatesTo));

        /* Small delay to reduce collision probability (per WS-Discovery spec). */
        cyg_thread_delay(pdMS_TO_TICKS(50));

        wsd_get_ip_str(ip_str, sizeof(ip_str));
        wsd_format_msgid(msgid, sizeof(msgid));

        /* Send ProbeMatch as UDP unicast back to the sender. */
        snprintf(sndbuf, 2048, WSD_PROBEMATCH_TMPL,
                 msgid, relatesTo, ip_str);
        lwip_sendto(sock, sndbuf, strlen(sndbuf), 0,
                    (struct sockaddr *)&from, fromlen);

        diag_printf("wsd_disc: ProbeMatch sent to %s\n",
                    inet_ntoa(from.sin_addr));
    }
    /* Not reached */
    free(sndbuf);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * WSD-Scan HTTP server — TCP port 5357
 *
 * Per-connection SOAP handler.  Reads the HTTP request, extracts the
 * SOAPAction or Body action, and builds the appropriate SOAP response.
 * ───────────────────────────────────────────────────────────────────────────*/

/*
 * Send an HTTP/1.0 response wrapping a SOAP XML body.
 * status_code: 200 for success, 500 for SOAP fault.
 */
static void wsd_send_soap(int fd, int status_code, const char *soap_body)
{
    char   hdr[256];
    int    hdr_len;
    size_t body_len = soap_body ? strlen(soap_body) : 0;
    const char *status_text = (status_code == 200) ? "OK" : "Internal Server Error";

    hdr_len = snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: application/soap+xml; charset=UTF-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code, status_text, body_len);

    lwip_send(fd, hdr, (size_t)hdr_len, 0);
    if (soap_body && body_len > 0) {
        lwip_send(fd, soap_body, body_len, 0);
    }
}

/*
 * Per-connection WSD HTTP handler.
 * Parses the incoming HTTP request and dispatches to the correct SOAP builder.
 */
static void handle_wsd_request(int fd)
{
    char  *rcvbuf;
    char  *rspbuf;
    int    n;
    char   msgid[64];
    char   relatesTo[256];
    char   ip_str[20];
    int    is_get;

    rcvbuf = (char *)malloc(4096);
    rspbuf = (char *)malloc(4096);
    if (!rcvbuf || !rspbuf) {
        free(rcvbuf);
        free(rspbuf);
        return;
    }

    n = lwip_recv(fd, rcvbuf, 4095, 0);
    if (n <= 0) {
        free(rcvbuf);
        free(rspbuf);
        return;
    }
    rcvbuf[n] = '\0';

    /* Determine if this is an HTTP GET (metadata request) or POST (SOAP). */
    is_get = (strncmp(rcvbuf, "GET ", 4) == 0);

    /* Extract RelatesTo from incoming MessageID (for SOAP responses). */
    wsd_extract_msgid(rcvbuf, n, relatesTo, sizeof(relatesTo));
    wsd_format_msgid(msgid, sizeof(msgid));
    wsd_get_ip_str(ip_str, sizeof(ip_str));

    if (is_get) {
        /* HTTP GET → return WS-Transfer GetResponse (device metadata). */
        snprintf(rspbuf, 4096, WSD_METADATA_TMPL,
                 msgid, relatesTo, ip_str);
        wsd_send_soap(fd, 200, rspbuf);

    } else if (wsd_contains(rcvbuf, n, "GetScannerElements")) {
        snprintf(rspbuf, 4096, WSD_SCANNER_ELEM_TMPL,
                 msgid, relatesTo);
        wsd_send_soap(fd, 200, rspbuf);

    } else if (wsd_contains(rcvbuf, n, "CreateScanJob")) {
        /*
         * Accept the scan job.  Actual USB scanner I/O is not yet
         * implemented — forwarding scan data from USB to the network
         * client is a TODO, matching the USB print forwarding stub in lpr.c.
         * TODO: trigger USB scanner and stream image data to client.
         */
        snprintf(rspbuf, 4096, WSD_CREATE_JOB_TMPL,
                 msgid, relatesTo);
        wsd_send_soap(fd, 200, rspbuf);

    } else if (wsd_contains(rcvbuf, n, "GetJobElements")) {
        snprintf(rspbuf, 4096, WSD_GET_JOB_TMPL,
                 msgid, relatesTo);
        wsd_send_soap(fd, 200, rspbuf);

    } else if (wsd_contains(rcvbuf, n, "RetrieveImage")) {
        /*
         * Image retrieval is not yet implemented because the USB scanner
         * driver has not been written.  Return a SOAP Fault with status
         * 500 so Windows retries rather than silently failing.
         * TODO: return actual scan data here once USB scanner driver exists.
         */
        snprintf(rspbuf, 4096, WSD_FAULT_TMPL,
                 msgid, relatesTo);
        wsd_send_soap(fd, 500, rspbuf);

    } else {
        /* Unknown/unimplemented SOAP action — return a generic SOAP Fault. */
        diag_printf("wsd_http: unknown action in request\n");
        snprintf(rspbuf, 4096, WSD_FAULT_TMPL,
                 msgid, "urn:uuid:unknown");
        wsd_send_soap(fd, 500, rspbuf);
    }

    free(rcvbuf);
    free(rspbuf);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * WSD-Scan HTTP connection pool — same pattern as ipp_server.c / escl_server.c
 * ───────────────────────────────────────────────────────────────────────────*/
typedef struct {
    int          fd;
    cyg_bool_t   in_use;
    cyg_handle_t thread;
    /*
     * Thread stack and TCB allocated dynamically by xTaskCreate; freed by the
     * FreeRTOS idle task via vTaskDelete(NULL).  See ipp_server.c for rationale.
     */
} wsd_conn_t;

static wsd_conn_t  wsd_pool[WSD_HTTP_MAX_CONN];
static cyg_mutex_t wsd_pool_lock;

static void wsd_http_child_thread(cyg_addrword_t arg)
{
    wsd_conn_t *conn = (wsd_conn_t *)arg;

    handle_wsd_request(conn->fd);
    lwip_close(conn->fd);

    cyg_mutex_lock(&wsd_pool_lock);
    conn->in_use = false;
    cyg_mutex_unlock(&wsd_pool_lock);

    cyg_thread_exit();
}

/* ─────────────────────────────────────────────────────────────────────────────
 * WSD-Scan HTTP server main thread — listens on port 5357
 * ───────────────────────────────────────────────────────────────────────────*/
void wsd_http_thread(cyg_addrword_t arg)
{
    int                server_fd;
    int                client_fd;
    struct sockaddr_in addr;
    int                opt = 1;
    cyg_uint32         i;

    (void)arg;

    /* Wait for the network to come up. */
    cyg_thread_delay(pdMS_TO_TICKS(2000));

    if (!g_scanner_enabled) {
        diag_printf("wsd_http: scanner disabled — thread exiting\n");
        return;
    }

    cyg_mutex_init(&wsd_pool_lock);
    memset(wsd_pool, 0, sizeof(wsd_pool));

    server_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        diag_printf("wsd_http: socket() failed\n");
        return;
    }
    lwip_setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(WSD_HTTP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (lwip_bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        diag_printf("wsd_http: bind() failed\n");
        lwip_close(server_fd);
        return;
    }
    if (lwip_listen(server_fd, WSD_HTTP_MAX_CONN) < 0) {
        diag_printf("wsd_http: listen() failed\n");
        lwip_close(server_fd);
        return;
    }

    diag_printf("wsd_http: listening on port %d (WSD-Scan)\n", WSD_HTTP_PORT);

    for (;;) {
        wsd_conn_t *slot = NULL;

        client_fd = lwip_accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            cyg_thread_delay(10);
            continue;
        }

        cyg_mutex_lock(&wsd_pool_lock);
        for (i = 0; i < WSD_HTTP_MAX_CONN; i++) {
            if (!wsd_pool[i].in_use) {
                slot = &wsd_pool[i];
                slot->in_use = true;
                slot->fd     = client_fd;
                break;
            }
        }
        cyg_mutex_unlock(&wsd_pool_lock);

        if (!slot) {
            diag_printf("wsd_http: No free connection slot\n");
            lwip_close(client_fd);
            continue;
        }

        {
            BaseType_t ret;
            ret = xTaskCreate(
                (TaskFunction_t)wsd_http_child_thread,
                "wsd_child",
                (configSTACK_DEPTH_TYPE)(WSD_HTTP_STACK_SIZE / sizeof(StackType_t)),
                (void *)slot,
                CYG_TO_FRT_PRIO(WSD_HTTP_PRIORITY),
                &slot->thread);
            if (ret != pdPASS) {
                diag_printf("wsd_http: xTaskCreate failed (out of heap?)\n");
                lwip_close(client_fd);
                cyg_mutex_lock(&wsd_pool_lock);
                slot->in_use = false;
                cyg_mutex_unlock(&wsd_pool_lock);
                continue;
            }
        }
    }
}
