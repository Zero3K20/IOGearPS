/*
 * mt7688_uart.h — MT7688 UART0 polled-I/O driver header.
 */

#ifndef MT7688_UART_H
#define MT7688_UART_H

void uart_putc(char c);
void uart_puts(const char *s);

#endif /* MT7688_UART_H */
