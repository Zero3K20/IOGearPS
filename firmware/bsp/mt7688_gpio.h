/*
 * mt7688_gpio.h — MT7688/MT7628 GPIO register definitions.
 *
 * All addresses are KSEG1 uncached aliases of the physical SoC registers so
 * that reads and writes bypass the D-cache and hit the hardware directly.
 *
 * Physical base addresses:
 *   System Control (SYSCTRL): 0x10000000  →  KSEG1: 0xB0000000
 *   GPIO controller:          0x10000600  →  KSEG1: 0xB0000600
 *
 * References:
 *   MT7628AN/MT7688 Datasheet §5 System Control, §7 GPIO
 *   OpenWRT target/linux/ramips/dts/mt7628*.dtsi
 *   Padavan GPL source: trunk/linux-3.4.x/arch/mips/ralink/mt7620.c
 */

#ifndef MT7688_GPIO_H
#define MT7688_GPIO_H

#include <stdint.h>

/* ── System Control — pinmux registers ─────────────────────────────────── */

/*
 * MT7688_GPIOMODE — KSEG1 alias of SYSCTRL+0x60.
 *
 * Each bit-group selects whether a GPIO-capable pin is driven by its primary
 * (peripheral) function or as a plain GPIO.  The original binary sets bit 14
 * during LED initialisation to put the relevant pin into GPIO mode.
 */
#define MT7688_GPIOMODE   (*(volatile uint32_t *)0xB0000060UL)

/*
 * MT7688_GPIOMODE2 — KSEG1 alias of SYSCTRL+0x64.
 *
 * Extended pinmux register for the second group of GPIO-capable pins.
 * The original binary writes 0x0555 (= bits 0,2,4,8,10) to select GPIO mode
 * for the pins connected to the back-panel LEDs (GPIO38, GPIO40–44).
 */
#define MT7688_GPIOMODE2  (*(volatile uint32_t *)0xB0000064UL)

/* ── GPIO controller ────────────────────────────────────────────────────── *
 *
 * GPIOCTRL1 — direction register for GPIO32–63 (KSEG1: 0xB0000604).
 *   Bit N set (1) → GPIO(32+N) is an output.
 *   Bit N clear (0) → GPIO(32+N) is an input.
 *
 * GPIODATA1 — data register for GPIO32–63 (KSEG1: 0xB0000624).
 *   Write 1 → drive the pin HIGH.
 *   Write 0 → drive the pin LOW.
 *   Read returns the current logic level on the pin.
 */
#define MT7688_GPIOCTRL1  (*(volatile uint32_t *)0xB0000604UL)
#define MT7688_GPIODATA1  (*(volatile uint32_t *)0xB0000624UL)

/* ── LED GPIO bit masks (within GPIOCTRL1 / GPIODATA1) ─────────────────── *
 *
 * The GPSU21 back-panel LEDs are wired active-low between the GPIO pin and
 * VCC: driving the pin LOW turns the LED on; driving it HIGH turns it off.
 *
 * GPIO38 = bit  6 of GPIOCTRL1/GPIODATA1 → USB / Printer-attached indicator
 * GPIO40 = bit  8 of GPIOCTRL1/GPIODATA1 → Status / Network-ready indicator
 * GPIO41 = bit  9 of GPIOCTRL1/GPIODATA1 → (reserved; configured as output)
 * GPIO42 = bit 10 of GPIOCTRL1/GPIODATA1 → (reserved; configured as output)
 * GPIO43 = bit 11 of GPIOCTRL1/GPIODATA1 → (reserved; configured as output)
 * GPIO44 = bit 12 of GPIOCTRL1/GPIODATA1 → (reserved; configured as output)
 *
 * Values extracted by disassembling the original ZOT binary firmware
 * (MPS56_90956F_9034_20191119.bin).
 */
#define MT7688_LED_USB_BIT      (1u <<  6)   /* GPIO38: USB/Printer LED      */
#define MT7688_LED_STATUS_BIT   (1u <<  8)   /* GPIO40: Status/Network LED   */

/* Bit mask for all output pins initialised during led_init(). */
#define MT7688_LED_OUTPUT_MASK  (0x1F00u | MT7688_LED_USB_BIT)
                              /* bits [12:8] + bit 6 = GPIO38,40–44 outputs  */

/* ── Pinmux values extracted from the original ZOT firmware ─────────────── *
 *
 * GPIOMODE bit 14: puts a shared pin into GPIO mode (required for GPIO38/40).
 * GPIOMODE2 value 0x0555: enables GPIO mode for the LED pin groups.
 */
#define MT7688_GPIOMODE_LED_BIT  (1u << 14)
#define MT7688_GPIOMODE2_LED_VAL 0x0555u

#endif /* MT7688_GPIO_H */
