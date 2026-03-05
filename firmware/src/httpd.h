/*
 * httpd.h — HTTP server interface for the GPSU21 firmware.
 */
#ifndef HTTPD_H
#define HTTPD_H

#include <cyg/kernel/kapi.h>

/* HTTP server thread entry — pass to cyg_thread_create(). */
void httpd_thread(cyg_addrword_t arg);

#endif /* HTTPD_H */
