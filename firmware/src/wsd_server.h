/*
 * wsd_server.h — WS-Discovery and WSD-Scan server interface.
 *
 * Two threads implement Windows WSD scanner discovery and scan operations:
 *
 *   wsd_discovery_thread — WS-Discovery responder on UDP port 3702.
 *       Joins the WSD multicast group (239.255.255.250), sends a Hello
 *       message on startup, and responds to Probe messages requesting
 *       scan:ScannerServiceType so that Windows auto-discovers the scanner.
 *
 *   wsd_http_thread — WSD-Scan SOAP server on TCP port 5357.
 *       Handles WS-Transfer Get (device metadata) and WSD-Scan operations
 *       (GetScannerElements, CreateScanJob, GetJobElements, RetrieveImage)
 *       so that Windows can read scanner capabilities and submit scan jobs.
 *
 * Both threads are enabled by the g_scanner_enabled configuration flag.
 * Actual USB scanner I/O is a TODO, matching the existing USB printing stub.
 */
#ifndef WSD_SERVER_H
#define WSD_SERVER_H

#include "rtos.h"

/* WS-Discovery responder — UDP multicast 239.255.255.250:3702 */
void wsd_discovery_thread(void *arg);

/* WSD-Scan HTTP server — TCP port 5357 */
void wsd_http_thread(void *arg);

#endif /* WSD_SERVER_H */
