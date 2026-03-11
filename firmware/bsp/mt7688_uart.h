/*
 * mt7688_uart.h — MT7688 UART0 polled-I/O driver and WDT helpers.
 */

#ifndef MT7688_UART_H
#define MT7688_UART_H

/* ── UART ───────────────────────────────────────────────────────────────── */
void uart_putc(char c);
void uart_puts(const char *s);

/* ── Hardware Watchdog Timer (MT7688/MT7628 SYSCTRL block) ──────────────── *
 *
 * The physical register base is 0x10000120.  All accesses use the KSEG1
 * uncached alias 0xB0000120 so that writes are not buffered by the cache.
 *
 * MT7688_WDT_TIMER — write 0 to disable, write a non-zero tick count to arm.
 * MT7688_WDT_RESET — write any non-zero value to kick (reload) the timer.
 *
 * The ZOT U-Boot bootloader arms the watchdog during its startup sequence
 * with a ~30-second timeout.  If the application firmware does not kick or
 * disable the WDT before the timeout expires, the SoC will reset, causing a
 * continuous reboot loop that appears as a bricked device.
 *
 * References: MT7628AN/MT7688 Datasheet § System Control (SYSCTRL),
 *             OpenWRT mt7620-wdt.c, Padavan RAETH drivers.
 */
#include <stdint.h>

/* MT7688_WDT_BASE — KSEG1 uncached alias of physical 0x10000120. */
#define MT7688_WDT_BASE   0xB0000120UL
#define MT7688_WDT_TIMER  (*(volatile uint32_t *)(MT7688_WDT_BASE + 0x00))
#define MT7688_WDT_RESET  (*(volatile uint32_t *)(MT7688_WDT_BASE + 0x04))

/*
 * mt7688_wdt_disable() — write 0 to the timer register, disabling the WDT.
 * Call once from board_init() before any other firmware code runs.
 */
static inline void mt7688_wdt_disable(void)
{
    MT7688_WDT_TIMER = 0;
}

/*
 * mt7688_wdt_keepalive() — keep the WDT disabled.
 * Call periodically from the watchdog task (every few hundred milliseconds).
 * Writing 0 to WDOG_TIMER prevents any re-arm by other code paths.
 */
static inline void mt7688_wdt_keepalive(void)
{
    MT7688_WDT_TIMER = 0;
}

/* ── MT7688 SYSCTRL soft reset ──────────────────────────────────────────── *
 *
 * The MT7688 SYSCTRL block provides a software reset register at
 * physical address 0x10000034 (KSEG1 uncached alias 0xB0000034).
 * Writing 1 to bit 0 (the SYSRST bit) triggers an immediate full SoC
 * reset, equivalent to a hardware power cycle.  The ZOT U-Boot bootloader
 * then re-runs from flash with a clean hardware state.
 *
 * This is used in panic/crash handlers (stack overflow, fatal exception) so
 * the device can reboot and recover rather than hanging permanently.  Without
 * a reset path, a firmware crash with the WDT disabled leaves the device in
 * an infinite spin — the web interface is unreachable and reflashing is
 * impossible, which appears as a bricked device.
 *
 * References: MT7628AN/MT7688 Datasheet § System Control (SYSCTRL),
 *             OpenWRT arch/mips/ralink/reset.c (ralink_machine_restart).
 */
#define MT7688_RSTCTRL          (*(volatile uint32_t *)(0xB0000034UL))
#define MT7688_RSTCTRL_SYSRST   (1u << 0)

/*
 * mt7688_soc_reset() — trigger an immediate full SoC reset.
 * Call from panic handlers after logging the error to UART.
 * Does not return.
 */
static inline void __attribute__((noreturn)) mt7688_soc_reset(void)
{
    MT7688_RSTCTRL |= MT7688_RSTCTRL_SYSRST;
    /* Should not be reached — SoC resets on the next bus cycle. */
    for (;;) { }
}

#endif /* MT7688_UART_H */
