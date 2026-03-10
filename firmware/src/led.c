/*
 * led.c — GPSU21 back-panel LED control (LightToggleProc).
 *
 * Implements the LED state machine from the original ZOT firmware binary
 * (MPS56_90956F_9034_20191119.bin), reverse-engineered from the MIPS32
 * disassembly of the LightToggleProc thread and its GPIO helper functions.
 *
 * Hardware:
 *   The GPSU21 has two active-low LEDs on its back panel, connected between
 *   VCC and the following MT7688 GPIO pins (via a current-limiting resistor):
 *
 *     GPIO38  (GPIOCTRL1/GPIODATA1 bit 6)  — USB / Printer indicator
 *     GPIO40  (GPIOCTRL1/GPIODATA1 bit 8)  — Status / Network indicator
 *
 *   Active-low: write 0 (LOW) to light the LED; write 1 (HIGH) to turn it off.
 *
 * Pinmux:
 *   Before the GPIO controller can drive these pins, they must be switched to
 *   GPIO function in the System Control pinmux registers.  The original binary
 *   sets GPIOMODE bit 14 and writes 0x0555 to GPIOMODE2.  GPIO40–44 are
 *   configured as outputs from the start; GPIO38 is added as an output only
 *   when a USB printer is detected.
 *
 * LED behaviour (matching the LightToggleProc state machine):
 *
 *   State                     USB LED (GPIO38)   Status LED (GPIO40)
 *   ─────────────────────────────────────────────────────────────────
 *   No network / DHCP wait    OFF                BLINKING (500 ms period)
 *   Network up, no printer    OFF                SOLID ON
 *   Printer connected         SOLID ON           SOLID ON
 *   Printer printing          SOLID ON           BLINKING (500 ms period)
 *
 * References:
 *   Original ZOT binary: led_init() at decompressed offset 0x6B8C,
 *   LED helper functions at 0x6CC8–0x6E10, LightToggleProc at 0x63D0.
 *   MT7688 GPIO registers: firmware/bsp/mt7688_gpio.h.
 */

#include "led.h"
#include "rtos.h"
#include "usb_printer.h"
#include "../bsp/mt7688_gpio.h"

#include "lwip/netif.h"
#include "lwip/dhcp.h"

#include <stdint.h>
#include <stdbool.h>

/* ── Internal helpers ──────────────────────────────────────────────────── */

/*
 * led_gpio_init() — configure pinmux and GPIO direction registers.
 *
 * Reproduces the exact sequence from the original binary's led_init() at
 * decompressed offset 0x6B8C:
 *
 *   *GPIOMODE  |= 0x4000     (bit 14 → pin in GPIO mode)
 *   *GPIOMODE2  = 0          (clear)
 *   *GPIOMODE2 |= 0x0554     (pin groups → GPIO mode)
 *   *GPIOMODE2 |= 0x0001     (complete: GPIOMODE2 = 0x0555)
 *   *GPIOCTRL1  = 0          (clear direction register)
 *   *GPIOCTRL1 |= 0x1F00     (GPIO40–44 as outputs)
 *   *GPIODATA1 |= 0x0100     (GPIO40 = 1 → Status LED initially off)
 */
static void led_gpio_init(void)
{
    /* Enable GPIO function for the LED pins via the pinmux registers. */
    MT7688_GPIOMODE  |= MT7688_GPIOMODE_LED_BIT;
    MT7688_GPIOMODE2  = 0;
    MT7688_GPIOMODE2 |= 0x0554u;
    MT7688_GPIOMODE2 |= 0x0001u;

    /* Configure GPIO40–44 as outputs; GPIO38 is added later when needed. */
    MT7688_GPIOCTRL1  = 0;
    MT7688_GPIOCTRL1 |= 0x1F00u;

    /* Drive GPIO40 HIGH → Status LED starts off. */
    MT7688_GPIODATA1 |= MT7688_LED_STATUS_BIT;
}

/*
 * led_usb_on() / led_usb_off() — control the USB/Printer indicator (GPIO38).
 *
 * led_usb_on() also ensures GPIO38 is configured as an output, mirroring the
 * original binary which defers GPIO38 direction setup until first use.
 */
static void led_usb_on(void)
{
    MT7688_GPIOCTRL1 |= MT7688_LED_USB_BIT;        /* GPIO38 → output */
    MT7688_GPIODATA1 &= ~(uint32_t)MT7688_LED_USB_BIT; /* LOW → LED on  */
}

static void led_usb_off(void)
{
    MT7688_GPIOCTRL1 |= MT7688_LED_USB_BIT;         /* GPIO38 → output  */
    MT7688_GPIODATA1 |= MT7688_LED_USB_BIT;         /* HIGH → LED off   */
}

/* led_status_on() / led_status_off() — control the Status/Network LED (GPIO40). */
static void led_status_on(void)
{
    MT7688_GPIODATA1 &= ~(uint32_t)MT7688_LED_STATUS_BIT; /* LOW → LED on  */
}

static void led_status_off(void)
{
    MT7688_GPIODATA1 |= MT7688_LED_STATUS_BIT;      /* HIGH → LED off   */
}

/* ── dhcp_has_address() ────────────────────────────────────────────────── *
 *
 * Returns true if the default lwIP network interface has been assigned an
 * IP address by DHCP (or a static address is configured).
 */
static bool dhcp_has_address(void)
{
    struct netif *nif = netif_default;
    if (nif == NULL) {
        return false;
    }
#if LWIP_DHCP
    /* dhcp_supplied_address() returns non-zero once DHCP has bound. */
    if (dhcp_supplied_address(nif)) {
        return true;
    }
#endif
    /* Also accept a non-zero static address (DHCP disabled or timed out). */
    return !ip4_addr_isany(netif_ip4_addr(nif));
}

/* ── LED state machine ─────────────────────────────────────────────────── */

/*
 * LED states, matching the LightToggleProc behaviour in the original binary.
 */
typedef enum {
    LED_STATE_NO_NETWORK = 0,  /* waiting for DHCP / no IP address        */
    LED_STATE_NETWORK_UP,      /* IP address obtained, no printer          */
    LED_STATE_PRINTER_IDLE,    /* USB printer connected, not printing      */
    LED_STATE_PRINTER_BUSY,    /* USB printer connected and printing       */
} led_state_t;

void led_thread(void *arg)
{
    bool        blink_phase  = false;
    uint32_t    blink_ticks  = 0;
    led_state_t state        = LED_STATE_NO_NETWORK;
    led_state_t prev_state   = (led_state_t)(-1);   /* force first update  */

    (void)arg;

    /* Initialise GPIO pins before any LED updates. */
    led_gpio_init();

    /* Short startup delay — let the network stack and USB host initialise. */
    cyg_thread_delay(pdMS_TO_TICKS(1000));

    diag_printf("LED: indicator thread started\n");

    for (;;) {
        bool printer_connected = usb_printer_is_connected();
        bool printer_busy      = printer_connected &&
                                 g_printer_status.busy;
        bool net_up            = dhcp_has_address();

        /* Determine the new LED state. */
        if (printer_busy) {
            state = LED_STATE_PRINTER_BUSY;
        } else if (printer_connected) {
            state = LED_STATE_PRINTER_IDLE;
        } else if (net_up) {
            state = LED_STATE_NETWORK_UP;
        } else {
            state = LED_STATE_NO_NETWORK;
        }

        /* Log state transitions. */
        if (state != prev_state) {
            switch (state) {
            case LED_STATE_NO_NETWORK:
                diag_printf("LED: no network — status LED blinking\n");
                break;
            case LED_STATE_NETWORK_UP:
                diag_printf("LED: network up — status LED solid\n");
                break;
            case LED_STATE_PRINTER_IDLE:
                diag_printf("LED: printer connected — both LEDs solid\n");
                break;
            case LED_STATE_PRINTER_BUSY:
                diag_printf("LED: printer busy — USB solid, status blinking\n");
                break;
            }
            prev_state  = state;
            blink_ticks = 0;
            blink_phase = false;
        }

        /* Apply the LED pattern for the current state. */
        switch (state) {
        case LED_STATE_NO_NETWORK:
            /*
             * Network not yet available: blink the Status LED at a 500 ms
             * period (250 ms on, 250 ms off).  USB LED stays off.
             */
            led_usb_off();
            if (blink_phase) {
                led_status_on();
            } else {
                led_status_off();
            }
            blink_ticks++;
            if (blink_ticks >= 1u) {       /* toggle every poll cycle       */
                blink_phase  = !blink_phase;
                blink_ticks  = 0;
            }
            break;

        case LED_STATE_NETWORK_UP:
            /* IP address acquired, no printer: Status LED solid on. */
            led_usb_off();
            led_status_on();
            break;

        case LED_STATE_PRINTER_IDLE:
            /* Printer connected and idle: both LEDs solid on. */
            led_usb_on();
            led_status_on();
            break;

        case LED_STATE_PRINTER_BUSY:
            /*
             * Printer actively printing: USB LED solid on, Status LED blinks
             * at a 500 ms period (250 ms on, 250 ms off) to indicate activity.
             */
            led_usb_on();
            if (blink_phase) {
                led_status_on();
            } else {
                led_status_off();
            }
            blink_ticks++;
            if (blink_ticks >= 1u) {
                blink_phase  = !blink_phase;
                blink_ticks  = 0;
            }
            break;
        }

        /* Poll every 250 ms. */
        cyg_thread_delay(pdMS_TO_TICKS(250));
    }
}
