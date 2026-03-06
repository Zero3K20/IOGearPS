/*
 * port.c — FreeRTOS MIPS32r2 C port functions for the IOGear GPSU21.
 *
 * Provides:
 *   pxPortInitialiseStack()   — lay down a fake MIPS32 context frame on a
 *                               new task's stack so portRESTORE_CONTEXT
 *                               starts the task correctly.
 *   xPortStartScheduler()     — program CP0 Count/Compare for the tick,
 *                               enable interrupts, and restore the first
 *                               task's context.
 *   vPortEndScheduler()       — not used; loops forever.
 *   vPortEnterCritical()      — increment nesting counter, DI.
 *   vPortExitCritical()       — decrement nesting counter, EI when zero.
 *   vPortYield()              — assert SW0 in CP0 Cause → context switch.
 *   vApplicationGetIdleTaskMemory()   — static idle task buffers.
 *   vApplicationGetTimerTaskMemory()  — static timer task buffers.
 *
 * The actual context save/restore and ISR handler live in port_asm.S.
 */

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

/* Prototype of assembly routine that restores the first task context. */
extern void _portRestoreContext(void);

/* ── Critical-section nesting counter ─────────────────────────────────── */
static UBaseType_t uxCriticalNesting = 0;

void vPortEnterCritical(void)
{
    __asm__ volatile("di; ehb" ::: "memory");
    uxCriticalNesting++;
}

void vPortExitCritical(void)
{
    configASSERT(uxCriticalNesting > 0);
    uxCriticalNesting--;
    if (uxCriticalNesting == 0) {
        __asm__ volatile("ei; ehb" ::: "memory");
    }
}

/* ── Yield via SW0 software interrupt ─────────────────────────────────── */
void vPortYield(void)
{
    uint32_t cause;
    __asm__ volatile("mfc0 %0, $13" : "=r"(cause));
    cause |= (1u << 8);   /* SW0 pending bit */
    __asm__ volatile("mtc0 %0, $13; ehb" :: "r"(cause) : "memory");
}

/* ── Stack initialisation ──────────────────────────────────────────────── *
 *
 * Layout of the fake context frame (portCONTEXT_WORDS = 32 words).
 * See port_asm.S for the matching save/restore code.
 *
 * Word  0 (lowest addr / SP after alloc): CP0 EPC  ← task entry point
 * Word  1: CP0 Status  (IE=1, EXL=1 → eret clears EXL & enables IRQs)
 * Word  2: HI = 0
 * Word  3: LO = 0
 * Word  4: AT ($1) = 0
 * Word  5: V0 ($2) = 0
 * Word  6: V1 ($3) = 0
 * Word  7: A0 ($4) = pvParameters   ← task argument
 * Word  8-10: A1-A3 = 0
 * Word 11-28: T0-T9, S0-S7, GP, FP  all 0 (except GP below)
 * Word 29: GP ($28) = _gp (set by linker, but we use 0 since -G 0)
 * Word 30: FP ($30) = 0
 * Word 31 (highest addr): RA ($31) = (uint32_t)prvTaskExitError
 */

/* Spin loop called if a task function returns (it should not). */
static void prvTaskExitError(void)
{
    /* Tasks must never return.  If one does, spin here so a debugger can
     * catch it.  The volatile prevents the compiler from optimising away
     * the loop. */
    volatile int spin = 1;
    while (spin) { }
}

StackType_t *pxPortInitialiseStack(StackType_t  *pxTopOfStack,
                                    TaskFunction_t pxCode,
                                    void          *pvParameters)
{
    /* Move down by one full context frame (32 words = 128 bytes). */
    pxTopOfStack -= portCONTEXT_WORDS;

    /* Zero the entire frame first. */
    {
        int i;
        for (i = 0; i < portCONTEXT_WORDS; i++) {
            pxTopOfStack[i] = 0;
        }
    }

    /*
     * CP0 EPC: task entry function.
     * Word 0 (offset 0) in the frame.
     */
    pxTopOfStack[0] = (StackType_t)(uintptr_t)pxCode;

    /*
     * CP0 Status initial value for each new task.
     *
     *  Bit  0  IE  = 1  — interrupts enabled once EXL is cleared by eret
     *  Bit  1  EXL = 1  — exception level; eret clears this bit
     *  Bit  8  IM0 = 1  — unmask IP0 / SW0 (software interrupt, used by vPortYield)
     *  Bit 15  IM7 = 1  — unmask IP7 / HW5 (CP0 Count/Compare timer → scheduler tick)
     *
     * IM0 and IM7 correspond to IP0 (SW0, bit 8 of Cause) and IP7 (HW5,
     * bit 15 of Cause) respectively — consistent with xPortStartScheduler()
     * and vPortISR in port_asm.S which both use the same bit positions.
     *
     * Without IM0 and IM7 the timer and yield interrupts are both permanently
     * masked after the first eret, so the FreeRTOS scheduler never ticks and
     * the device appears completely unresponsive (bricked).
     *
     * Value: 0x00008103 (IE | EXL | IM0 | IM7).
     */
    pxTopOfStack[1] = (StackType_t)0x00008103UL;

    /* A0 ($4) = task argument, at word 7. */
    pxTopOfStack[7] = (StackType_t)(uintptr_t)pvParameters;

    /* RA ($31) = task-exit error handler, at word 31. */
    pxTopOfStack[31] = (StackType_t)(uintptr_t)prvTaskExitError;

    /* Return the new stack pointer (pointing to bottom of frame = word 0). */
    return pxTopOfStack;
}

/* ── Scheduler start ───────────────────────────────────────────────────── */
BaseType_t xPortStartScheduler(void)
{
    uint32_t count;

    /* Verify that the assembly-mode portCOMPARE_DELTA constant matches
     * the C-mode derived value.  Update the constant in portmacro.h if
     * configCPU_CLOCK_HZ or configTICK_RATE_HZ is changed. */
    _Static_assert(
        2900000UL == (configCPU_CLOCK_HZ / 2UL / configTICK_RATE_HZ),
        "portCOMPARE_DELTA assembly constant does not match FreeRTOSConfig.h"
    );

    /* Reset the critical-section nesting counter. */
    uxCriticalNesting = 0;

    /* Program CP0 Compare for the first tick interrupt.
     * The timer ISR (vPortISR in port_asm.S) reprograms Compare on each tick
     * by adding portCOMPARE_DELTA to the current Count value. */
    __asm__ volatile("mfc0 %0, $9" : "=r"(count));      /* read Count */
    count += (uint32_t)portCOMPARE_DELTA;
    __asm__ volatile("mtc0 %0, $11; ehb" :: "r"(count)); /* write Compare */

    /*
     * Enable the timer interrupt (HW5 = IP7, IM7 = bit 15 in Status).
     * Also enable SW0 (IM0 = bit 8) for yield.
     * Set IE=1.  Leave EXL=0, ERL=0.
     */
    {
        uint32_t status;
        __asm__ volatile("mfc0 %0, $12" : "=r"(status));
        /* Set IM7 (bit 15 = timer HW5) and IM0 (bit 8 = SW0) and IE (bit 0). */
        status |= (1u << 15) | (1u << 8) | (1u << 0);
        /* Clear EXL and ERL so we can run in normal mode. */
        status &= ~((1u << 1) | (1u << 2));
        __asm__ volatile("mtc0 %0, $12; ehb" :: "r"(status) : "memory");
    }

    /* Restore the context of the first task — does not return. */
    _portRestoreContext();

    /* Should never reach here. */
    return pdFALSE;
}

void vPortEndScheduler(void)
{
    /* Not implemented. */
    for (;;);
}

/* ── Static memory for the Idle task ───────────────────────────────────── */
static StaticTask_t  xIdleTaskTCB;
static StackType_t   xIdleTaskStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                    StackType_t  **ppxIdleTaskStackBuffer,
                                    uint32_t      *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer   = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = xIdleTaskStack;
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

/* ── Static memory for the Timer task ──────────────────────────────────── */
static StaticTask_t  xTimerTaskTCB;
static StackType_t   xTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                     StackType_t  **ppxTimerTaskStackBuffer,
                                     uint32_t      *pulTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer   = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = xTimerTaskStack;
    *pulTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
}
