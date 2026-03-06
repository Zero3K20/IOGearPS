/*
 * mt7688_uart.c — MT7688 UART0 polled-I/O driver.
 *
 * The ZOT bootloader has already configured UART0 at 57600-8N1 using the
 * MT7688 UART0 base address 0xB0000C00 (KSEG1, uncached).  This driver
 * provides polled transmit access used by printf() via the newlib _write()
 * syscall stub in syscalls.c.
 *
 * Register layout (32-bit registers at 4-byte stride, 16550-compatible):
 *   Base + 0x00  RBR/THR  Receive buffer / Transmit holding
 *   Base + 0x14  LSR      Line status
 *     LSR bit 5 (THRE): Transmit Holding Register Empty
 *     LSR bit 6 (TEMT): Transmitter Empty
 */

#include <stdint.h>
#include "mt7688_uart.h"

#define MT7688_UART0_BASE   0xB0000C00UL

#define UART_THR   (*(volatile uint32_t *)(MT7688_UART0_BASE + 0x00))
#define UART_LSR   (*(volatile uint32_t *)(MT7688_UART0_BASE + 0x14))

#define UART_LSR_THRE  0x20u   /* Transmit Holding Register Empty */
#define UART_LSR_TEMT  0x40u   /* Transmitter Empty               */

/* Send one character, blocking until the UART FIFO has room.
 * Translates '\n' → "\r\n" for terminal compatibility. */
void uart_putc(char c)
{
    if (c == '\n') {
        while (!(UART_LSR & UART_LSR_THRE))
            ;
        UART_THR = (uint32_t)'\r';
    }
    while (!(UART_LSR & UART_LSR_THRE))
        ;
    UART_THR = (uint32_t)(unsigned char)c;
}

void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}
