/*
 * hal_diag.h — HAL diagnostic I/O interface for the MediaTek MT7688 SoC.
 *
 * This header is required by the eCos infrastructure package
 * (CYGPKG_INFRA, packages/infra/v3_0/src/diag.cxx) which does:
 *
 *   #include <cyg/hal/hal_diag.h>
 *
 * and then calls HAL_DIAG_WRITE_CHAR() and HAL_DIAG_INIT() to route
 * diagnostic output (diag_printf / CYG_FAIL etc.) to the platform
 * console.
 *
 * The actual UART I/O is implemented in mt7688_startup.c:
 *   hal_diag_write_char(char c)   — write one byte to UART0
 *   hal_diag_read_char(char *c)   — read one byte from UART0
 *
 * The bootloader (U-Boot / ZOT loader) has already configured UART0
 * at 57600-8N1 before handing control to eCos, so no re-initialisation
 * is required here.
 */

#ifndef CYGONCE_HAL_DIAG_H
#define CYGONCE_HAL_DIAG_H

#include <pkgconf/hal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations for the UART helper functions in mt7688_startup.c. */
extern void hal_diag_write_char(char c);
extern void hal_diag_read_char(char *c);

#ifdef __cplusplus
}
#endif

/*
 * HAL_DIAG_INIT() — called by the eCos infrastructure before the first
 * diagnostic character is written.  UART0 is already set up by U-Boot,
 * so nothing needs to be done here.
 */
#define HAL_DIAG_INIT()             do { } while (0)

/*
 * HAL_DIAG_WRITE_CHAR(_c_) — write a single character to the UART.
 * '\n' is expanded to "\r\n" inside hal_diag_write_char().
 */
#define HAL_DIAG_WRITE_CHAR(_c_)    hal_diag_write_char(_c_)

/*
 * HAL_DIAG_READ_CHAR(_c_) — read a single character from the UART.
 * Blocks until a byte is available.
 */
#define HAL_DIAG_READ_CHAR(_c_)     hal_diag_read_char(&(_c_))

#endif /* CYGONCE_HAL_DIAG_H */
/* End of hal_diag.h */
