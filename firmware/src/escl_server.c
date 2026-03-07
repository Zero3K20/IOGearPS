/*
 * escl_server.c — eSCL (AirScan) scanner server for the IOGear GPSU21.
 *
 * Listens on TCP port 9290 and implements the eSCL (Mopria Scan) protocol
 * so that iOS 13+ and macOS 10.15+ clients can discover and use the scanner
 * on a connected USB multi-function device via AirScan.
 *
 * eSCL is an HTTP-based REST protocol.  This server handles the mandatory
 * endpoints:
 *
 *   GET  /eSCL/ScannerCapabilities  — returns scanner capabilities XML
 *   GET  /eSCL/ScannerStatus        — returns current scanner state XML
 *   POST /eSCL/ScanJobs             — creates a scan job (accepted; USB
 *                                     forwarding is a TODO, like printing)
 *   GET  /eSCL/ScanJobs/{id}/NextDocument
 *                                   — returns 503 until USB scanner driver
 *                                     is implemented
 *   DELETE /eSCL/ScanJobs/{id}      — cancels a scan job
 *
 * Scanner discovery is handled by mdns.c which advertises the device as
 * _uscan._tcp when g_scanner_enabled is non-zero.
 *
 * References:
 *   Apple eSCL Specification (Mopria Scan Service Implementation Guide)
 *   Namespace: http://schemas.hp.com/imaging/escl/2011/05/03
 */

#include "rtos.h"

#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <lwip/def.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include "escl_server.h"
#include "config.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Configuration
 * ───────────────────────────────────────────────────────────────────────────*/
#define ESCL_PORT               9290
#define ESCL_MAX_CONNECTIONS    4
#define ESCL_THREAD_STACK_SIZE  4096
#define ESCL_THREAD_PRIORITY    12

/* ─────────────────────────────────────────────────────────────────────────────
 * eSCL XML responses (static strings — kept small for embedded use)
 *
 * Namespace prefix "scan:" maps to the eSCL schema URI used by Apple AirScan.
 * ───────────────────────────────────────────────────────────────────────────*/

/* ScannerCapabilities — describes supported scan modes, resolutions, formats */
static const char ESCL_CAPABILITIES_XML[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
    "<scan:ScannerCapabilities"
    " xmlns:scan=\"http://schemas.hp.com/imaging/escl/2011/05/03\""
    " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\r\n"
    "  <scan:Version>2.63</scan:Version>\r\n"
    "  <scan:MakeAndModel>IOGear GPSU21</scan:MakeAndModel>\r\n"
    "  <scan:Manufacturer>IOGear</scan:Manufacturer>\r\n"
    "  <scan:SerialNumber>00001243</scan:SerialNumber>\r\n"
    "  <scan:UUID>00000000-0000-0000-0000-000000001243</scan:UUID>\r\n"
    "  <scan:AdminURI>http://gpsu21.local/</scan:AdminURI>\r\n"
    "  <scan:Platen>\r\n"
    "    <scan:PlatenInputCaps>\r\n"
    "      <scan:MinWidth>50</scan:MinWidth>\r\n"
    "      <scan:MaxWidth>2550</scan:MaxWidth>\r\n"
    "      <scan:MinHeight>50</scan:MinHeight>\r\n"
    "      <scan:MaxHeight>3508</scan:MaxHeight>\r\n"
    "      <scan:MaxScanRegions>1</scan:MaxScanRegions>\r\n"
    "      <scan:SettingProfiles>\r\n"
    "        <scan:SettingProfile>\r\n"
    "          <scan:ColorModes>\r\n"
    "            <scan:ColorMode>RGB24</scan:ColorMode>\r\n"
    "            <scan:ColorMode>Grayscale8</scan:ColorMode>\r\n"
    "            <scan:ColorMode>BlackAndWhite1</scan:ColorMode>\r\n"
    "          </scan:ColorModes>\r\n"
    "          <scan:ContentTypes>\r\n"
    "            <scan:ContentType>Photo</scan:ContentType>\r\n"
    "            <scan:ContentType>Text</scan:ContentType>\r\n"
    "            <scan:ContentType>TextAndPhoto</scan:ContentType>\r\n"
    "          </scan:ContentTypes>\r\n"
    "          <scan:DocumentFormats>\r\n"
    "            <scan:DocumentFormat>image/jpeg</scan:DocumentFormat>\r\n"
    "            <scan:DocumentFormat>image/png</scan:DocumentFormat>\r\n"
    "            <scan:DocumentFormat>application/pdf</scan:DocumentFormat>\r\n"
    "          </scan:DocumentFormats>\r\n"
    "          <scan:SupportedResolutions>\r\n"
    "            <scan:DiscreteResolutions>\r\n"
    "              <scan:DiscreteResolution>\r\n"
    "                <scan:XResolution>75</scan:XResolution>\r\n"
    "                <scan:YResolution>75</scan:YResolution>\r\n"
    "              </scan:DiscreteResolution>\r\n"
    "              <scan:DiscreteResolution>\r\n"
    "                <scan:XResolution>150</scan:XResolution>\r\n"
    "                <scan:YResolution>150</scan:YResolution>\r\n"
    "              </scan:DiscreteResolution>\r\n"
    "              <scan:DiscreteResolution>\r\n"
    "                <scan:XResolution>300</scan:XResolution>\r\n"
    "                <scan:YResolution>300</scan:YResolution>\r\n"
    "              </scan:DiscreteResolution>\r\n"
    "            </scan:DiscreteResolutions>\r\n"
    "          </scan:SupportedResolutions>\r\n"
    "          <scan:ColorSpaces>\r\n"
    "            <scan:ColorSpace>sRGB</scan:ColorSpace>\r\n"
    "          </scan:ColorSpaces>\r\n"
    "        </scan:SettingProfile>\r\n"
    "      </scan:SettingProfiles>\r\n"
    "    </scan:PlatenInputCaps>\r\n"
    "  </scan:Platen>\r\n"
    "</scan:ScannerCapabilities>\r\n";

/* ScannerStatus — reports current scanner state */
static const char ESCL_STATUS_IDLE_XML[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
    "<scan:ScannerStatus"
    " xmlns:scan=\"http://schemas.hp.com/imaging/escl/2011/05/03\""
    " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\r\n"
    "  <scan:Version>2.63</scan:Version>\r\n"
    "  <scan:State>Idle</scan:State>\r\n"
    "  <scan:Jobs/>\r\n"
    "</scan:ScannerStatus>\r\n";

/* ScanJob created response (201 Created body, optional per spec) */
static const char ESCL_JOB_CREATED_XML[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
    "<scan:ScanJob"
    " xmlns:scan=\"http://schemas.hp.com/imaging/escl/2011/05/03\">\r\n"
    "  <scan:JobUri>/eSCL/ScanJobs/1</scan:JobUri>\r\n"
    "  <scan:JobUuid>00000000-0000-0000-0000-000000000001</scan:JobUuid>\r\n"
    "</scan:ScanJob>\r\n";

/* ─────────────────────────────────────────────────────────────────────────────
 * HTTP response helpers
 * ───────────────────────────────────────────────────────────────────────────*/

static void escl_send_response(int fd, int status, const char *status_msg,
                               const char *content_type,
                               const char *extra_headers,
                               const char *body)
{
    char hdr[512];
    int  hdr_len;
    size_t body_len = body ? strlen(body) : 0;

    hdr_len = snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "%s"
        "Connection: close\r\n"
        "\r\n",
        status, status_msg,
        content_type,
        body_len,
        extra_headers ? extra_headers : "");

    lwip_send(fd, hdr, (size_t)hdr_len, 0);
    if (body && body_len > 0) {
        lwip_send(fd, body, body_len, 0);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Read the HTTP request line and headers from the socket.
 * Fills method (max 8 bytes), path (max 128 bytes).
 * Returns 0 on success, -1 on error.
 * ───────────────────────────────────────────────────────────────────────────*/
static int escl_read_request(int fd, char *method, char *path)
{
    char buf[1024];
    int  n;
    char proto[16];

    n = lwip_recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        return -1;
    }
    buf[n] = '\0';

    if (sscanf(buf, "%7s %127s %15s", method, path, proto) < 2) {
        return -1;
    }
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Per-connection request handler
 * ───────────────────────────────────────────────────────────────────────────*/
static void handle_escl_request(int fd)
{
    char method[8];
    char path[128];

    if (escl_read_request(fd, method, path) < 0) {
        return;
    }

    diag_printf("escl: %s %s\n", method, path);

    /* GET /eSCL/ScannerCapabilities */
    if (strcmp(method, "GET") == 0 &&
        strcmp(path, "/eSCL/ScannerCapabilities") == 0) {
        escl_send_response(fd, 200, "OK",
            "text/xml; charset=utf-8", NULL,
            ESCL_CAPABILITIES_XML);
        return;
    }

    /* GET /eSCL/ScannerStatus */
    if (strcmp(method, "GET") == 0 &&
        strcmp(path, "/eSCL/ScannerStatus") == 0) {
        escl_send_response(fd, 200, "OK",
            "text/xml; charset=utf-8", NULL,
            ESCL_STATUS_IDLE_XML);
        return;
    }

    /* POST /eSCL/ScanJobs — accept a new scan job */
    if (strcmp(method, "POST") == 0 &&
        strcmp(path, "/eSCL/ScanJobs") == 0) {
        /*
         * A real implementation would read the ScanSettings XML from the
         * request body, enumerate the USB scanner, and initiate a scan.
         * For now this is stubbed — the job is accepted and a Location
         * header is returned so that the client can poll for the document.
         * TODO: forward scan request to USB scanner device.
         */
        escl_send_response(fd, 201, "Created",
            "text/xml; charset=utf-8",
            "Location: /eSCL/ScanJobs/1\r\n",
            ESCL_JOB_CREATED_XML);
        return;
    }

    /* GET /eSCL/ScanJobs/{id}/NextDocument — return next scanned page */
    if (strcmp(method, "GET") == 0 &&
        strncmp(path, "/eSCL/ScanJobs/", 15) == 0 &&
        strstr(path, "/NextDocument") != NULL) {
        /*
         * USB scanner forwarding is not yet implemented.  Return 503 so
         * that the client retries; a future implementation will return the
         * scanned image data (JPEG/PNG/PDF) from the USB scanner here.
         * TODO: read scanned data from USB scanner and stream to client.
         */
        escl_send_response(fd, 503, "Service Unavailable",
            "text/plain",
            "Retry-After: 2\r\n",
            "Scanner device not ready — USB scanner driver not yet implemented.\r\n");
        return;
    }

    /* DELETE /eSCL/ScanJobs/{id} — cancel a scan job */
    if (strcmp(method, "DELETE") == 0 &&
        strncmp(path, "/eSCL/ScanJobs/", 15) == 0) {
        escl_send_response(fd, 200, "OK", "text/plain", NULL, "");
        return;
    }

    /* Unknown path */
    escl_send_response(fd, 404, "Not Found", "text/plain", NULL,
                       "Not Found\r\n");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Connection pool
 * ───────────────────────────────────────────────────────────────────────────*/
typedef struct {
    int          fd;
    cyg_bool_t   in_use;
    cyg_handle_t thread;
    /*
     * Thread stack and TCB are allocated dynamically by xTaskCreate so that
     * FreeRTOS frees them via the idle task after vTaskDelete(NULL).
     * See ipp_server.c for a full explanation of why static allocation must
     * not be used here.
     */
} escl_conn_t;

static escl_conn_t  escl_pool[ESCL_MAX_CONNECTIONS];
static cyg_mutex_t  escl_pool_lock;

static void escl_child_thread(void *arg)
{
    escl_conn_t *conn = (escl_conn_t *)arg;

    handle_escl_request(conn->fd);
    lwip_close(conn->fd);

    cyg_mutex_lock(&escl_pool_lock);
    conn->in_use = false;
    cyg_mutex_unlock(&escl_pool_lock);

    cyg_thread_exit();
}

/* ─────────────────────────────────────────────────────────────────────────────
 * eSCL server main thread — listens on port 9290
 * ───────────────────────────────────────────────────────────────────────────*/
void escl_server_thread(void *arg)
{
    int                server_fd;
    int                client_fd;
    struct sockaddr_in addr;
    int                opt = 1;
    cyg_uint32         i;

    (void)arg;

    /* Wait until the scanner feature is enabled and the network is up. */
    cyg_thread_delay(pdMS_TO_TICKS(2000));

    if (!g_scanner_enabled) {
        diag_printf("escl: scanner disabled — thread exiting\n");
        return;
    }

    cyg_mutex_init(&escl_pool_lock);
    memset(escl_pool, 0, sizeof(escl_pool));

    server_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        diag_printf("escl: socket() failed\n");
        return;
    }
    lwip_setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(ESCL_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (lwip_bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        diag_printf("escl: bind() failed\n");
        lwip_close(server_fd);
        return;
    }
    if (lwip_listen(server_fd, ESCL_MAX_CONNECTIONS) < 0) {
        diag_printf("escl: listen() failed\n");
        lwip_close(server_fd);
        return;
    }

    diag_printf("escl: listening on port %d (AirScan)\n", ESCL_PORT);

    for (;;) {
        escl_conn_t *slot = NULL;

        client_fd = lwip_accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            cyg_thread_delay(10);
            continue;
        }

        cyg_mutex_lock(&escl_pool_lock);
        for (i = 0; i < ESCL_MAX_CONNECTIONS; i++) {
            if (!escl_pool[i].in_use) {
                slot = &escl_pool[i];
                slot->in_use = true;
                slot->fd     = client_fd;
                break;
            }
        }
        cyg_mutex_unlock(&escl_pool_lock);

        if (!slot) {
            diag_printf("escl: No free connection slot\n");
            lwip_close(client_fd);
            continue;
        }

        {
            BaseType_t ret;
            ret = xTaskCreate(
                escl_child_thread,
                "escl_child",
                (configSTACK_DEPTH_TYPE)(ESCL_THREAD_STACK_SIZE / sizeof(StackType_t)),
                (void *)slot,
                CYG_TO_FRT_PRIO(ESCL_THREAD_PRIORITY),
                &slot->thread);
            if (ret != pdPASS) {
                diag_printf("escl: xTaskCreate failed (out of heap?)\n");
                lwip_close(client_fd);
                cyg_mutex_lock(&escl_pool_lock);
                slot->in_use = false;
                cyg_mutex_unlock(&escl_pool_lock);
                continue;
            }
        }
    }
}
