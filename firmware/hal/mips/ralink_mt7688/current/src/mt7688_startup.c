/*
 * mt7688_startup.c — Platform startup code for the MediaTek MT7688 SoC.
 *
 * This file provides the eCos platform initialisation routines that are
 * called early in the boot process by the MIPS architecture HAL
 * (CYGPKG_HAL_MIPS_MIPS32).  It implements:
 *
 *   hal_platform_init()   — called after cache/MMU init, before C runtime
 *   hal_diag_write_char() — low-level UART output for diag_printf()
 *   hal_diag_read_char()  — low-level UART input
 *
 * Hardware assumptions:
 *   The ZOT / U-Boot bootloader has already initialised:
 *     - SDRAM controller (64 MB at 0x00000000 physical / 0x80000000 KSEG0)
 *     - UART0 at 57600-8N1 (base 0xB0000C00)
 *     - Clocks (CPU 580 MHz, bus 193 MHz)
 *   We therefore only need to make UART0 available for diagnostic output;
 *   full SoC re-initialisation is left to the application layer.
 */

#include <pkgconf/system.h>
#include <pkgconf/hal.h>
#include <pkgconf/hal_mips_ralink_mt7688.h>

#include <cyg/hal/hal_arch.h>
#include <cyg/hal/hal_io.h>
#include <cyg/infra/cyg_type.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * MT7688 UART0 register map
 * Physical base: 0x10000C00
 * KSEG1 uncached alias: 0xB0000C00  (used here for safe access before cache on)
 * Register width: 32 bits, 4-byte stride.
 * ───────────────────────────────────────────────────────────────────────────*/
#define MT7688_UART0_BASE   0xB0000C00UL

#define UART_RBR    (MT7688_UART0_BASE + 0x00)  /* Receive buffer / transmit */
#define UART_THR    (MT7688_UART0_BASE + 0x00)  /* Transmit holding register */
#define UART_IER    (MT7688_UART0_BASE + 0x04)  /* Interrupt enable */
#define UART_FCR    (MT7688_UART0_BASE + 0x08)  /* FIFO control */
#define UART_LCR    (MT7688_UART0_BASE + 0x0C)  /* Line control */
#define UART_MCR    (MT7688_UART0_BASE + 0x10)  /* Modem control */
#define UART_LSR    (MT7688_UART0_BASE + 0x14)  /* Line status */

#define UART_LSR_DR     0x01    /* Data ready */
#define UART_LSR_THRE   0x20    /* Transmit holding register empty */
#define UART_LSR_TEMT   0x40    /* Transmitter empty */

/* ─────────────────────────────────────────────────────────────────────────────
 * hal_platform_init()
 *
 * Called by the eCos MIPS32 HAL after cache initialisation.  The bootloader
 * has already set up the UART, so this is a no-op — we rely on the existing
 * UART configuration for early diagnostic output.
 * ───────────────────────────────────────────────────────────────────────────*/
void hal_platform_init(void)
{
    /* UART0 is already initialised by the bootloader at 57600-8N1.
     * Flush any pending transmit data before eCos takes over. */
    volatile cyg_uint32 *lsr = (volatile cyg_uint32 *)UART_LSR;
    while (!(*lsr & UART_LSR_TEMT))
        ;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * hal_diag_write_char() — write one character to UART0.
 *
 * Implements the eCos virtual-vector diagnostic channel.  Called by
 * diag_printf() / diag_write_char() in the CYGPKG_INFRA package.
 * ───────────────────────────────────────────────────────────────────────────*/
void hal_diag_write_char(char c)
{
    volatile cyg_uint32 *lsr = (volatile cyg_uint32 *)UART_LSR;
    volatile cyg_uint32 *thr = (volatile cyg_uint32 *)UART_THR;

    /* Convert '\n' → "\r\n" for terminal compatibility */
    if (c == '\n') {
        while (!(*lsr & UART_LSR_THRE))
            ;
        *thr = '\r';
    }
    while (!(*lsr & UART_LSR_THRE))
        ;
    *thr = (cyg_uint32)(unsigned char)c;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * hal_diag_read_char() — read one character from UART0.
 * ───────────────────────────────────────────────────────────────────────────*/
void hal_diag_read_char(char *c)
{
    volatile cyg_uint32 *lsr = (volatile cyg_uint32 *)UART_LSR;
    volatile cyg_uint32 *rbr = (volatile cyg_uint32 *)UART_RBR;

    while (!(*lsr & UART_LSR_DR))
        ;
    *c = (char)(*rbr & 0xFF);
}
