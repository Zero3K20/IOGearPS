/*
 * portmacro.h — FreeRTOS MIPS32r2 port macros for the IOGear GPSU21.
 *
 * Target: MediaTek MT7688 (MIPS24KEc, MIPS32r2, little-endian, no FPU)
 *
 * This file is included by FreeRTOS.h via the standard portmacro.h
 * discovery path.  It defines:
 *   - Primitive types (StackType_t, BaseType_t, TickType_t …)
 *   - Critical-section macros (using MIPS32r2 di/ei instructions)
 *   - Context-switch macro (triggers SW0 software interrupt)
 *   - Timer constants (CP0 Count/Compare delta for the tick)
 */

#ifndef PORTMACRO_H
#define PORTMACRO_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── Primitive types ───────────────────────────────────────────────────── */
#define portCHAR        char
#define portFLOAT       float
#define portDOUBLE      double
#define portLONG        long
#define portSHORT       short

/* Stack entries are 32-bit words on MIPS32. */
#define portSTACK_TYPE  unsigned long
#define portBASE_TYPE   long

#ifndef __ASSEMBLER__
#include <stdint.h>

typedef portSTACK_TYPE  StackType_t;
typedef long            BaseType_t;
typedef unsigned long   UBaseType_t;

#if (configUSE_16_BIT_TICKS == 1)
typedef uint16_t TickType_t;
#define portMAX_DELAY   ((TickType_t)0xFFFFU)
#else
typedef uint32_t TickType_t;
#define portMAX_DELAY   ((TickType_t)0xFFFFFFFFUL)
#define portTICK_TYPE_IS_ATOMIC 1
#endif /* configUSE_16_BIT_TICKS */
#endif /* !__ASSEMBLER__ */

/* ── Architecture constants ────────────────────────────────────────────── */
#define portSTACK_GROWTH        (-1)        /* stack grows downward          */
#define portTICK_PERIOD_MS      ((TickType_t)1000UL / configTICK_RATE_HZ)
#define portBYTE_ALIGNMENT      8           /* MIPS EABI: 8-byte aligned     */
#define portNOP()               __asm__ volatile("nop")

/* ── CP0 Count/Compare tick delta ─────────────────────────────────────── *
 *
 * MIPS24KEc increments CP0 Count every 2 CPU cycles.
 *   Count rate = 580 000 000 / 2 = 290 000 000 Hz
 *   Delta      = 290 000 000 / 100 = 2 900 000 ticks per tick interrupt
 *
 * For the C build this is derived from FreeRTOSConfig.h constants.
 * For assembly builds we use a plain integer literal (no UL suffix)
 * because the GNU assembler expression evaluator does not understand
 * C integer suffixes.
 */
#ifdef __ASSEMBLER__
#define portCOMPARE_DELTA  2900000   /* 580MHz/2/100Hz — plain integer */
#else
#define portCOMPARE_DELTA  ((uint32_t)(configCPU_CLOCK_HZ / 2UL / configTICK_RATE_HZ))
#endif

/* ── Context size ──────────────────────────────────────────────────────── *
 * 32 words (128 bytes): see port_asm.S for layout details.
 */
#define portCONTEXT_WORDS   32
#define portCONTEXT_BYTES   (portCONTEXT_WORDS * 4)

/* Register offsets within the saved context frame (SP-relative). */
#define ctx_EPC     ( 0 * 4)
#define ctx_STATUS  ( 1 * 4)
#define ctx_HI      ( 2 * 4)
#define ctx_LO      ( 3 * 4)
#define ctx_AT      ( 4 * 4)
#define ctx_V0      ( 5 * 4)
#define ctx_V1      ( 6 * 4)
#define ctx_A0      ( 7 * 4)
#define ctx_A1      ( 8 * 4)
#define ctx_A2      ( 9 * 4)
#define ctx_A3      (10 * 4)
#define ctx_T0      (11 * 4)
#define ctx_T1      (12 * 4)
#define ctx_T2      (13 * 4)
#define ctx_T3      (14 * 4)
#define ctx_T4      (15 * 4)
#define ctx_T5      (16 * 4)
#define ctx_T6      (17 * 4)
#define ctx_T7      (18 * 4)
#define ctx_S0      (19 * 4)
#define ctx_S1      (20 * 4)
#define ctx_S2      (21 * 4)
#define ctx_S3      (22 * 4)
#define ctx_S4      (23 * 4)
#define ctx_S5      (24 * 4)
#define ctx_S6      (25 * 4)
#define ctx_S7      (26 * 4)
#define ctx_T8      (27 * 4)
#define ctx_T9      (28 * 4)
#define ctx_GP      (29 * 4)
#define ctx_FP      (30 * 4)
#define ctx_RA      (31 * 4)

/* ── Critical sections ─────────────────────────────────────────────────── *
 *
 * MIPS32r2 provides the DI / EI instructions for atomic disable/enable of
 * the global interrupt enable bit (CP0 Status.IE).  The nesting counter is
 * maintained by the port; portENTER_CRITICAL / portEXIT_CRITICAL are C
 * inline wrappers in port.c.
 */
#ifndef __ASSEMBLER__
extern void vPortEnterCritical(void);
extern void vPortExitCritical(void);

#define portENTER_CRITICAL()    vPortEnterCritical()
#define portEXIT_CRITICAL()     vPortExitCritical()

/* Bare interrupt disable/enable for ISR code (no nesting counter). */
#define portDISABLE_INTERRUPTS() \
    __asm__ volatile("di; ehb" ::: "memory")

#define portENABLE_INTERRUPTS() \
    __asm__ volatile("ei; ehb" ::: "memory")

/* ISR-safe critical section — same as bare disable/enable on MIPS32. */
#define portSET_INTERRUPT_MASK_FROM_ISR()    \
    ({ uint32_t _s; __asm__ volatile("di %0; ehb" : "=r"(_s)); _s; })

#define portCLEAR_INTERRUPT_MASK_FROM_ISR(s) \
    do { if ((s) & 1u) { __asm__ volatile("ei; ehb" ::: "memory"); } } while(0)
#endif /* !__ASSEMBLER__ */

/* ── Yield ──────────────────────────────────────────────────────────────── *
 * Trigger a context switch by asserting the SW0 software-interrupt pending
 * bit in CP0 Cause.  The interrupt handler (vPortISR) will see SW0 and
 * call vTaskSwitchContext().
 */
#ifndef __ASSEMBLER__
extern void vPortYield(void);
#define portYIELD()  vPortYield()
#endif

/* ── Task function prototype ────────────────────────────────────────────── */
#define portTASK_FUNCTION_PROTO(fn, arg)  void fn(void *arg)
#define portTASK_FUNCTION(fn, arg)        void fn(void *arg)

/* ── Miscellaneous ──────────────────────────────────────────────────────── */
#define portINLINE   __inline__

#ifdef __cplusplus
}
#endif

#endif /* PORTMACRO_H */
