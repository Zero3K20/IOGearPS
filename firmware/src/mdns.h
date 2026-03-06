/*
 * mdns.h — mDNS/Bonjour advertising interface.
 */
#ifndef MDNS_H
#define MDNS_H

#include "rtos.h"

void mdns_thread(cyg_addrword_t arg);

#endif /* MDNS_H */
