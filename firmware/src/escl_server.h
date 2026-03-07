/*
 * escl_server.h — eSCL (AirScan) scanner server interface.
 *
 * The eSCL server listens on TCP port 9290 and implements Apple's eSCL
 * (Mopria Scan) protocol so that iOS 13+ and macOS 10.15+ clients can
 * discover and use the scanner on a connected USB multi-function device.
 */
#ifndef ESCL_SERVER_H
#define ESCL_SERVER_H

#include "rtos.h"

void escl_server_thread(void *arg);

#endif /* ESCL_SERVER_H */
