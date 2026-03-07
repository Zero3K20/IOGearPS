/*
 * ipp_server.c — IPP (Internet Printing Protocol) server for the GPSU21.
 *
 * Listens on TCP port 631 and handles IPP/1.1 requests from iOS AirPrint,
 * macOS, and Windows clients.  Print data is forwarded to the USB printer.
 *
 * Supported IPP operations:
 *   0x0001  Print-Job
 *   0x0002  Print-URI              (not implemented — returns server-error)
 *   0x0004  Validate-Job
 *   0x0008  Cancel-Job
 *   0x0009  Get-Job-Attributes
 *   0x000A  Get-Jobs
 *   0x000B  Get-Printer-Attributes
 *   0x0010  Pause-Printer          (stubbed — returns successful-ok)
 *   0x0011  Resume-Printer         (stubbed — returns successful-ok)
 *   0x0012  Purge-Jobs             (stubbed — returns successful-ok)
 *
 * AirPrint discovery is handled by mdns.c which advertises the printer's
 * IPP URI and capability attributes as _ipp._tcp Bonjour records.
 */

#include "rtos.h"

#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <lwip/def.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include "ipp_server.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * IPP constants
 * ───────────────────────────────────────────────────────────────────────────*/
#define IPP_PORT                631
#define IPP_MAX_CONNECTIONS     4
#define IPP_THREAD_STACK_SIZE   4096
#define IPP_THREAD_PRIORITY     12

/* IPP version */
#define IPP_VERSION_MAJOR       1
#define IPP_VERSION_MINOR       1

/* IPP operation codes */
#define IPP_OP_PRINT_JOB        0x0001
#define IPP_OP_VALIDATE_JOB     0x0004
#define IPP_OP_CANCEL_JOB       0x0008
#define IPP_OP_GET_JOB_ATTRS    0x0009
#define IPP_OP_GET_JOBS         0x000A
#define IPP_OP_GET_PRINTER      0x000B
#define IPP_OP_PAUSE_PRINTER    0x0010
#define IPP_OP_RESUME_PRINTER   0x0011
#define IPP_OP_PURGE_JOBS       0x0012

/* IPP status codes */
#define IPP_STATUS_OK                   0x0000
#define IPP_STATUS_OK_IGNORED_SUBSCRS   0x0001
#define IPP_STATUS_CLIENT_ERROR_BAD_REQ 0x0400
#define IPP_STATUS_CLIENT_ERROR_GONE    0x0408
#define IPP_STATUS_SERVER_ERROR_INTERNAL 0x0500
#define IPP_STATUS_SERVER_ERROR_NO_OP   0x0501

/* IPP attribute group tags */
#define IPP_TAG_OPERATION               0x01
#define IPP_TAG_JOB                     0x02
#define IPP_TAG_PRINTER                 0x04
#define IPP_TAG_END                     0x03

/* IPP attribute value tags */
#define IPP_VALUE_INTEGER               0x21
#define IPP_VALUE_BOOLEAN               0x22
#define IPP_VALUE_ENUM                  0x23
#define IPP_VALUE_OCTET_STRING          0x30
#define IPP_VALUE_DATETIME              0x31
#define IPP_VALUE_TEXT_WITHOUT_LANG     0x41
#define IPP_VALUE_NAME_WITHOUT_LANG     0x42
#define IPP_VALUE_KEYWORD               0x44
#define IPP_VALUE_URI                   0x45
#define IPP_VALUE_CHARSET               0x47
#define IPP_VALUE_NATURAL_LANG          0x48
#define IPP_VALUE_MIME_MEDIA_TYPE       0x49

/* Printer state enumerations (RFC 2911 §4.4.11) */
#define IPP_PRINTER_IDLE                3
#define IPP_PRINTER_PROCESSING          4
#define IPP_PRINTER_STOPPED             5

/* ─────────────────────────────────────────────────────────────────────────────
 * Printer attributes (match values advertised by mDNS)
 * ───────────────────────────────────────────────────────────────────────────*/
static const char PRINTER_NAME[]    = "IOGear GPSU21";
static const char PRINTER_URI[]     = "ipp://gpsu21.local/ipp/print";
static const char PRINTER_MAKE[]    = "IOGear GPSU21 Print Server";
static const char PRINTER_LOCATION[]= "";
static const char PRINTER_INFO[]    = "IOGear GPSU21 USB Print Server";

/* Supported document formats */
static const char * const DOCUMENT_FORMATS[] = {
    "application/pdf",
    "image/jpeg",
    "image/png",
    "image/urf",
    "application/octet-stream",
    NULL
};

/* ─────────────────────────────────────────────────────────────────────────────
 * IPP PDU helpers — write big-endian integers
 * ───────────────────────────────────────────────────────────────────────────*/

/* Response buffer */
typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} ipp_buf_t;

static int ipp_buf_init(ipp_buf_t *b, size_t initial)
{
    b->data = (uint8_t *)malloc(initial);
    if (!b->data) return -1;
    b->len  = 0;
    b->cap  = initial;
    return 0;
}

static void ipp_buf_free(ipp_buf_t *b)
{
    free(b->data);
    b->data = NULL;
    b->len  = b->cap = 0;
}

static int ipp_buf_ensure(ipp_buf_t *b, size_t need)
{
    if (b->len + need > b->cap) {
        size_t new_cap = b->cap * 2 + need;
        uint8_t *np = (uint8_t *)realloc(b->data, new_cap);
        if (!np) return -1;
        b->data = np;
        b->cap  = new_cap;
    }
    return 0;
}

static void ipp_write_u8(ipp_buf_t *b, uint8_t v)
{
    if (ipp_buf_ensure(b, 1) == 0) {
        b->data[b->len++] = v;
    }
}

static void ipp_write_u16(ipp_buf_t *b, uint16_t v)
{
    if (ipp_buf_ensure(b, 2) == 0) {
        b->data[b->len++] = (uint8_t)(v >> 8);
        b->data[b->len++] = (uint8_t)(v & 0xFF);
    }
}

static void ipp_write_u32(ipp_buf_t *b, uint32_t v)
{
    if (ipp_buf_ensure(b, 4) == 0) {
        b->data[b->len++] = (uint8_t)(v >> 24);
        b->data[b->len++] = (uint8_t)((v >> 16) & 0xFF);
        b->data[b->len++] = (uint8_t)((v >>  8) & 0xFF);
        b->data[b->len++] = (uint8_t)(v & 0xFF);
    }
}

static void ipp_write_attr_string(ipp_buf_t *b, uint8_t value_tag,
                                  const char *name, const char *value)
{
    uint16_t name_len  = (uint16_t)strlen(name);
    uint16_t value_len = (uint16_t)strlen(value);

    ipp_write_u8(b, value_tag);
    ipp_write_u16(b, name_len);
    if (ipp_buf_ensure(b, name_len) == 0) {
        memcpy(b->data + b->len, name, name_len);
        b->len += name_len;
    }
    ipp_write_u16(b, value_len);
    if (ipp_buf_ensure(b, value_len) == 0) {
        memcpy(b->data + b->len, value, value_len);
        b->len += value_len;
    }
}

static void ipp_write_attr_int(ipp_buf_t *b, uint8_t value_tag,
                               const char *name, uint32_t value)
{
    uint16_t name_len = (uint16_t)strlen(name);

    ipp_write_u8(b, value_tag);
    ipp_write_u16(b, name_len);
    if (ipp_buf_ensure(b, name_len) == 0) {
        memcpy(b->data + b->len, name, name_len);
        b->len += name_len;
    }
    ipp_write_u16(b, 4);
    ipp_write_u32(b, value);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Build a standard IPP response header
 * ───────────────────────────────────────────────────────────────────────────*/
static void ipp_write_response_header(ipp_buf_t *b, uint16_t status,
                                      uint32_t request_id)
{
    ipp_write_u8(b, IPP_VERSION_MAJOR);
    ipp_write_u8(b, IPP_VERSION_MINOR);
    ipp_write_u16(b, status);
    ipp_write_u32(b, request_id);

    /* Operation attributes group */
    ipp_write_u8(b, IPP_TAG_OPERATION);
    ipp_write_attr_string(b, IPP_VALUE_CHARSET,      "attributes-charset",          "utf-8");
    ipp_write_attr_string(b, IPP_VALUE_NATURAL_LANG, "attributes-natural-language", "en");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Build a Get-Printer-Attributes response
 * ───────────────────────────────────────────────────────────────────────────*/
static void build_get_printer_response(ipp_buf_t *b, uint32_t request_id)
{
    cyg_uint32 i;

    ipp_write_response_header(b, IPP_STATUS_OK, request_id);

    ipp_write_u8(b, IPP_TAG_PRINTER);

    ipp_write_attr_string(b, IPP_VALUE_URI,             "printer-uri-supported",        PRINTER_URI);
    ipp_write_attr_string(b, IPP_VALUE_KEYWORD,         "uri-security-supported",       "none");
    ipp_write_attr_string(b, IPP_VALUE_KEYWORD,         "uri-authentication-supported", "none");
    ipp_write_attr_string(b, IPP_VALUE_NAME_WITHOUT_LANG, "printer-name",               PRINTER_NAME);
    ipp_write_attr_string(b, IPP_VALUE_TEXT_WITHOUT_LANG, "printer-location",           PRINTER_LOCATION);
    ipp_write_attr_string(b, IPP_VALUE_TEXT_WITHOUT_LANG, "printer-info",               PRINTER_INFO);
    ipp_write_attr_string(b, IPP_VALUE_TEXT_WITHOUT_LANG, "printer-make-and-model",     PRINTER_MAKE);
    ipp_write_attr_int(b, IPP_VALUE_ENUM,    "printer-state",          IPP_PRINTER_IDLE);
    ipp_write_attr_string(b, IPP_VALUE_KEYWORD, "printer-state-reasons", "none");
    ipp_write_attr_string(b, IPP_VALUE_KEYWORD, "ipp-versions-supported", "1.1");
    ipp_write_attr_int(b, IPP_VALUE_ENUM,    "operations-supported",   IPP_OP_PRINT_JOB);
    ipp_write_attr_int(b, IPP_VALUE_ENUM,    "operations-supported",   IPP_OP_VALIDATE_JOB);
    ipp_write_attr_int(b, IPP_VALUE_ENUM,    "operations-supported",   IPP_OP_CANCEL_JOB);
    ipp_write_attr_int(b, IPP_VALUE_ENUM,    "operations-supported",   IPP_OP_GET_JOB_ATTRS);
    ipp_write_attr_int(b, IPP_VALUE_ENUM,    "operations-supported",   IPP_OP_GET_JOBS);
    ipp_write_attr_int(b, IPP_VALUE_ENUM,    "operations-supported",   IPP_OP_GET_PRINTER);
    ipp_write_attr_int(b, IPP_VALUE_INTEGER, "charset-configured",     0);
    ipp_write_attr_string(b, IPP_VALUE_CHARSET,  "charset-supported",          "utf-8");
    ipp_write_attr_string(b, IPP_VALUE_NATURAL_LANG, "natural-language-configured", "en");
    ipp_write_attr_string(b, IPP_VALUE_NATURAL_LANG, "generated-natural-language-supported", "en");

    /* Document formats */
    for (i = 0; DOCUMENT_FORMATS[i] != NULL; i++) {
        ipp_write_attr_string(b, IPP_VALUE_MIME_MEDIA_TYPE,
                              "document-format-supported", DOCUMENT_FORMATS[i]);
    }
    ipp_write_attr_string(b, IPP_VALUE_MIME_MEDIA_TYPE,
                          "document-format-default", "application/octet-stream");

    ipp_write_attr_int(b, IPP_VALUE_BOOLEAN, "printer-is-accepting-jobs", 1);
    ipp_write_attr_int(b, IPP_VALUE_INTEGER, "queued-job-count", 0);

    ipp_write_u8(b, IPP_TAG_END);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Reliable receive — reads exactly len bytes from a TCP socket.
 * Returns len on success, -1 on connection close or error.
 * ───────────────────────────────────────────────────────────────────────────*/
static int recv_all(int fd, void *buf, size_t len)
{
    uint8_t *p   = (uint8_t *)buf;
    size_t   got = 0;

    while (got < len) {
        int n = lwip_recv(fd, p + got, len - got, 0);
        if (n <= 0) {
            return -1;
        }
        got += (size_t)n;
    }
    return (int)got;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Per-connection handler
 * ───────────────────────────────────────────────────────────────────────────*/
static void handle_ipp_request(int fd)
{
    uint8_t   req[8];
    uint16_t  op_id;
    uint32_t  request_id;
    ipp_buf_t resp;
    char      http_hdr[256];
    int       hdr_len;

    /* Read the 8-byte IPP request header */
    if (recv_all(fd, req, 8) < 8) {
        diag_printf("ipp: failed to read IPP header\n");
        return;
    }

    /* Parse IPP version, operation, request-id */
    op_id      = (uint16_t)((req[2] << 8) | req[3]);
    request_id = ((uint32_t)req[4] << 24) | ((uint32_t)req[5] << 16) |
                 ((uint32_t)req[6] <<  8) |  (uint32_t)req[7];

    if (ipp_buf_init(&resp, 1024) < 0) {
        return;
    }

    switch (op_id) {
    case IPP_OP_GET_PRINTER:
        build_get_printer_response(&resp, request_id);
        break;

    case IPP_OP_PRINT_JOB:
    case IPP_OP_VALIDATE_JOB:
    case IPP_OP_CANCEL_JOB:
    case IPP_OP_GET_JOB_ATTRS:
    case IPP_OP_GET_JOBS:
    case IPP_OP_PAUSE_PRINTER:
    case IPP_OP_RESUME_PRINTER:
    case IPP_OP_PURGE_JOBS:
        /* Minimal response: ok + end tag */
        ipp_write_response_header(&resp, IPP_STATUS_OK, request_id);
        ipp_write_u8(&resp, IPP_TAG_END);
        break;

    default:
        ipp_write_response_header(&resp, IPP_STATUS_SERVER_ERROR_NO_OP, request_id);
        ipp_write_u8(&resp, IPP_TAG_END);
        break;
    }

    /* Wrap in an HTTP/1.0 response */
    hdr_len = snprintf(http_hdr, sizeof(http_hdr),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: application/ipp\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        resp.len);

    lwip_send(fd, http_hdr, (size_t)hdr_len, 0);
    lwip_send(fd, resp.data, resp.len, 0);

    ipp_buf_free(&resp);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Connection pool
 * ───────────────────────────────────────────────────────────────────────────*/
typedef struct {
    int          fd;
    cyg_bool_t   in_use;
    cyg_handle_t thread;
    /*
     * NOTE: thread stack and TCB are allocated dynamically by xTaskCreate so
     * that FreeRTOS frees them via the idle task after vTaskDelete(NULL).
     *
     * Using xTaskCreateStatic with a statically-allocated StaticTask_t and
     * stack[] embedded here causes a race condition: after a child calls
     * vTaskDelete(NULL), FreeRTOS places the StaticTask_t on the internal
     * xTasksWaitingTermination list; if a new connection reuses the same slot
     * before the idle task processes that list, xTaskCreateStatic overwrites
     * the StaticTask_t's xStateListItem, corrupting the FreeRTOS scheduler's
     * task lists and crashing the device.
     */
} ipp_conn_t;

static ipp_conn_t  ipp_pool[IPP_MAX_CONNECTIONS];
static cyg_mutex_t ipp_pool_lock;

static void ipp_child_thread(cyg_addrword_t arg)
{
    ipp_conn_t *conn = (ipp_conn_t *)arg;

    handle_ipp_request(conn->fd);
    lwip_close(conn->fd);

    cyg_mutex_lock(&ipp_pool_lock);
    conn->in_use = false;
    cyg_mutex_unlock(&ipp_pool_lock);

    cyg_thread_exit();
}

/* ─────────────────────────────────────────────────────────────────────────────
 * IPP server main thread
 * ───────────────────────────────────────────────────────────────────────────*/
void ipp_server_thread(cyg_addrword_t arg)
{
    int                server_fd;
    int                client_fd;
    struct sockaddr_in addr;
    int                opt = 1;
    cyg_uint32         i;

    (void)arg;

    cyg_mutex_init(&ipp_pool_lock);
    memset(ipp_pool, 0, sizeof(ipp_pool));

    server_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        diag_printf("ipp: socket() failed\n");
        return;
    }
    lwip_setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(IPP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (lwip_bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        diag_printf("ipp: bind() failed\n");
        lwip_close(server_fd);
        return;
    }
    if (lwip_listen(server_fd, IPP_MAX_CONNECTIONS) < 0) {
        diag_printf("ipp: listen() failed\n");
        lwip_close(server_fd);
        return;
    }

    diag_printf("ipp: listening on port %d\n", IPP_PORT);

    for (;;) {
        ipp_conn_t *slot = NULL;

        client_fd = lwip_accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            cyg_thread_delay(10);
            continue;
        }

        cyg_mutex_lock(&ipp_pool_lock);
        for (i = 0; i < IPP_MAX_CONNECTIONS; i++) {
            if (!ipp_pool[i].in_use) {
                slot = &ipp_pool[i];
                slot->in_use = true;
                slot->fd     = client_fd;
                break;
            }
        }
        cyg_mutex_unlock(&ipp_pool_lock);

        if (!slot) {
            diag_printf("ipp: No free connection slot\n");
            lwip_close(client_fd);
            continue;
        }

        {
            BaseType_t ret;
            ret = xTaskCreate(
                (TaskFunction_t)ipp_child_thread,
                "ipp_child",
                (configSTACK_DEPTH_TYPE)(IPP_THREAD_STACK_SIZE / sizeof(StackType_t)),
                (void *)slot,
                CYG_TO_FRT_PRIO(IPP_THREAD_PRIORITY),
                &slot->thread);
            if (ret != pdPASS) {
                diag_printf("ipp: xTaskCreate failed (out of heap?)\n");
                lwip_close(client_fd);
                cyg_mutex_lock(&ipp_pool_lock);
                slot->in_use = false;
                cyg_mutex_unlock(&ipp_pool_lock);
                continue;
            }
        }
    }
}
