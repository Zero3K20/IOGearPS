/*
 * ipp_server.h — IPP/AirPrint server interface.
 */
#ifndef IPP_SERVER_H
#define IPP_SERVER_H

#include "rtos.h"

void ipp_server_thread(cyg_addrword_t arg);

#endif /* IPP_SERVER_H */
