/*
 * mt7688_init.c — MT7688 board initialisation for FreeRTOS.
 *
 * Called from startup.S before main().  Responsibilities:
 *   1. Install exception-vector jump-stubs in the DRAM region at 0x80000000
 *      (below our load address 0x80500000).
 *   2. Set CP0 Cause.IV=1 so hardware interrupts use EBase+0x200 rather
 *      than the general-exception vector at EBase+0x180.
 *   3. Clear BEV in CP0 Status so the vectors we just installed are live.
 *
 * Also provides vApplicationGetIdleTaskMemory / vApplicationGetTimerTaskMemory
 * stubs (declared weak; port.c provides the actual definitions).
 */

#include <stdint.h>
#include "mt7688_uart.h"

/* ── Exception-vector stub installation ─────────────────────────────────── *
 *
 * MIPS32 j-instruction encoding:
 *   bits 31:26 = 0b000010  (j opcode = 0x08000000)
 *   bits 25:0  = target[27:2]  (26-bit word index)
 *
 * The j instruction branches to:
 *   { PC+4[31:28], instr_index[25:0], 2'b00 }
 *
 * Since all code lives in KSEG0 (0x80xxxxxx), PC+4[31:28] = 0x8 and
 * target[31:28] = 0x8 — the same 256 MB region — so the encoding is valid.
 */
static void write_j_stub(volatile uint32_t *dst, void (*target)(void))
{
    uint32_t addr = (uint32_t)(uintptr_t)target;
    uint32_t j_instr = 0x08000000u | ((addr >> 2) & 0x03FFFFFFu);
                  /* ^ j opcode = 0b000010 << 26   ^ 26-bit word-index */

    dst[0] = j_instr;       /* j  target  */
    dst[1] = 0x00000000u;   /* nop        */

    /* Flush D-cache lines and invalidate I-cache for the two words written.
     * synci (MIPS32r2) performs both operations atomically. */
    __asm__ volatile(
        "sync              \n"   /* complete all prior stores             */
        "synci  0(%0)      \n"   /* flush+invalidate cache line at dst[0] */
        "synci  4(%0)      \n"   /* flush+invalidate cache line at dst[1] */
        "sync              \n"   /* serialise                             */
        :: "r"(dst) : "memory"
    );
}

/* Forward declaration of the FreeRTOS interrupt handler (port_asm.S). */
extern void vPortISR(void);

/* Panic handler — installed for unexpected exceptions. */
static void __attribute__((noreturn)) exception_panic(void)
{
    uart_puts("\r\n*** FATAL EXCEPTION ***\r\n");
    for (;;)
        ;
}

void board_init(void)
{
    uint32_t reg;

    /*
     * Disable the MT7688 hardware watchdog FIRST, before anything else.
     *
     * The ZOT U-Boot bootloader arms the SoC watchdog with a ~30-second
     * timeout as part of its own startup sequence.  If our firmware does not
     * kick or disable the WDT within that window, the SoC resets, the
     * bootloader re-arms the WDT, and the cycle repeats — the device appears
     * permanently bricked even though only the WDT is at fault.
     *
     * Writing 0 to MT7688_WDT_TIMER disables the watchdog immediately.
     * The watchdog_thread in main.c periodically calls mt7688_wdt_keepalive()
     * to ensure the WDT stays disabled for the lifetime of the firmware.
     */
    mt7688_wdt_disable();

    /*
     * Install exception-vector stubs at their MIPS32 BEV=0 offsets.
     * EBase defaults to 0x80000000; offset layout:
     *   +0x000  TLB Refill        → panic (KSEG0/1 fixed-map, no TLB needed)
     *   +0x180  General Exception → panic
     *   +0x200  Interrupt         → vPortISR  (Cause.IV=1)
     */
    write_j_stub((volatile uint32_t *)0x80000000UL,
                 (void (*)(void))exception_panic);
    write_j_stub((volatile uint32_t *)0x80000180UL,
                 (void (*)(void))exception_panic);
    write_j_stub((volatile uint32_t *)0x80000200UL,
                 (void (*)(void))vPortISR);

    /* Set Cause.IV=1 so hardware interrupts use EBase+0x200 and do NOT
     * go through the general-exception vector at EBase+0x180. */
    __asm__ volatile("mfc0 %0, $13" : "=r"(reg));
    reg |= (1u << 23);                          /* Cause.IV bit */
    __asm__ volatile("mtc0 %0, $13; ehb" :: "r"(reg) : "memory");

    /* Clear BEV in CP0 Status → switch from bootstrap vectors to our own. */
    __asm__ volatile("mfc0 %0, $12" : "=r"(reg));
    reg &= ~(1u << 22);                         /* clear BEV */
    __asm__ volatile("mtc0 %0, $12; ehb" :: "r"(reg) : "memory");
}
