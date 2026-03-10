/*
 * led.h — GPSU21 back-panel LED control interface.
 *
 * The GPSU21 has two active-low LEDs on its back panel, driven via the
 * MT7688 GPIO controller:
 *
 *   GPIO38 (bit 6 of GPIODATA1)  — USB / Printer-attached indicator
 *   GPIO40 (bit 8 of GPIODATA1)  — Status / Network-ready indicator
 *
 * Active-low means the GPIO pin must be driven LOW to illuminate the LED and
 * HIGH to extinguish it.
 *
 * The led_thread() function implements the LightToggleProc behaviour from the
 * original ZOT firmware: it blinks the Status LED while the network is coming
 * up, holds it solid once an IP address is obtained, and additionally lights
 * the USB LED whenever a printer is attached.
 */

#ifndef LED_H
#define LED_H

/*
 * led_thread() — FreeRTOS task entry point for the LED state machine.
 *
 * Must be created via cyg_thread_create() in cyg_user_start() before the
 * scheduler is started.  The thread runs at low priority (below networking
 * threads) because LED updates are not time-critical.
 *
 * The thread never returns; it loops indefinitely polling USB printer and
 * network state and updating the GPIO-driven LEDs accordingly.
 */
void led_thread(void *arg);

#endif /* LED_H */
