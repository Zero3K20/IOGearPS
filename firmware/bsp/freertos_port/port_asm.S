/*
 * port_asm.S — FreeRTOS MIPS32r2 context-switch assembly for the MT7688.
 *
 * Provides:
 *   vPortISR           — interrupt vector handler installed at EBase+0x200
 *                        (Cause.IV=1); handles timer (HW5) and yield (SW0).
 *   _portRestoreContext — loads context of first task, ends with eret.
 *
 * Context frame layout (32 words = 128 bytes, SP points to word 0):
 *
 *  Word  0: CP0 EPC       (exception-return / task entry address)
 *  Word  1: CP0 Status    (IE=1, EXL=1 when saved; eret clears EXL)
 *  Word  2: HI
 *  Word  3: LO
 *  Word  4: AT  ($1)
 *  Word  5: V0  ($2)
 *  Word  6: V1  ($3)
 *  Word  7: A0  ($4)   ← task argument / first parameter
 *  Word  8: A1  ($5)
 *  Word  9: A2  ($6)
 *  Word 10: A3  ($7)
 *  Word 11: T0  ($8)
 *  Word 12: T1  ($9)
 *  Word 13: T2  ($10)
 *  Word 14: T3  ($11)
 *  Word 15: T4  ($12)
 *  Word 16: T5  ($13)
 *  Word 17: T6  ($14)
 *  Word 18: T7  ($15)
 *  Word 19: S0  ($16)
 *  Word 20: S1  ($17)
 *  Word 21: S2  ($18)
 *  Word 22: S3  ($19)
 *  Word 23: S4  ($20)
 *  Word 24: S5  ($21)
 *  Word 25: S6  ($22)
 *  Word 26: S7  ($23)
 *  Word 27: T8  ($24)
 *  Word 28: T9  ($25)
 *  Word 29: GP  ($28)
 *  Word 30: FP/S8 ($30)
 *  Word 31: RA  ($31)
 */

/* Pull in portCOMPARE_DELTA and context-offset constants. */
#include "portmacro.h"

    .set    noreorder
    .set    noat            /* we manage $at ourselves */

/* =========================================================================
 * portSAVE_CONTEXT macro
 *
 * Called from exception context (K0/K1 are free, EXL=1).
 * Allocates the context frame on the current task's stack, saves all
 * registers, then stores SP into pxCurrentTCB[0].
 * ========================================================================= */
.macro portSAVE_CONTEXT
    addiu   $sp, $sp, -(portCONTEXT_BYTES)

    sw      $31, ctx_RA($sp)
    sw      $30, ctx_FP($sp)
    sw      $28, ctx_GP($sp)
    sw      $25, ctx_T9($sp)
    sw      $24, ctx_T8($sp)
    sw      $23, ctx_S7($sp)
    sw      $22, ctx_S6($sp)
    sw      $21, ctx_S5($sp)
    sw      $20, ctx_S4($sp)
    sw      $19, ctx_S3($sp)
    sw      $18, ctx_S2($sp)
    sw      $17, ctx_S1($sp)
    sw      $16, ctx_S0($sp)
    sw      $15, ctx_T7($sp)
    sw      $14, ctx_T6($sp)
    sw      $13, ctx_T5($sp)
    sw      $12, ctx_T4($sp)
    sw      $11, ctx_T3($sp)
    sw      $10, ctx_T2($sp)
    sw      $9,  ctx_T1($sp)
    sw      $8,  ctx_T0($sp)
    sw      $7,  ctx_A3($sp)
    sw      $6,  ctx_A2($sp)
    sw      $5,  ctx_A1($sp)
    sw      $4,  ctx_A0($sp)
    sw      $3,  ctx_V1($sp)
    sw      $2,  ctx_V0($sp)
    sw      $1,  ctx_AT($sp)

    mfhi    $k0
    sw      $k0, ctx_HI($sp)
    mflo    $k0
    sw      $k0, ctx_LO($sp)

    mfc0    $k0, $12            /* CP0 Status */
    sw      $k0, ctx_STATUS($sp)
    mfc0    $k0, $14            /* CP0 EPC */
    sw      $k0, ctx_EPC($sp)

    /* Store the new SP value into the current task's TCB (TCB[0] = SP). */
    la      $k0, pxCurrentTCB
    lw      $k0, 0($k0)
    sw      $sp, 0($k0)
.endm

/* =========================================================================
 * portRESTORE_CONTEXT macro
 *
 * Loads SP from pxCurrentTCB[0] (which may have been updated by
 * vTaskSwitchContext to point to a different task), then restores all
 * registers and returns to the task via eret.
 * ========================================================================= */
.macro portRESTORE_CONTEXT
    /* Load new task's SP from TCB. */
    la      $k0, pxCurrentTCB
    lw      $k0, 0($k0)
    lw      $sp, 0($k0)

    /* Restore CP0 EPC and Status first (while EXL is still 1). */
    lw      $k0, ctx_EPC($sp)
    mtc0    $k0, $14            /* CP0 EPC */
    lw      $k0, ctx_STATUS($sp)
    mtc0    $k0, $12            /* CP0 Status (EXL=1 keeps IRQs off until eret) */
    ehb

    /* Restore HI/LO. */
    lw      $k0, ctx_LO($sp)
    mtlo    $k0
    lw      $k0, ctx_HI($sp)
    mthi    $k0

    /* Restore GPRs. */
    lw      $1,  ctx_AT($sp)
    lw      $2,  ctx_V0($sp)
    lw      $3,  ctx_V1($sp)
    lw      $4,  ctx_A0($sp)
    lw      $5,  ctx_A1($sp)
    lw      $6,  ctx_A2($sp)
    lw      $7,  ctx_A3($sp)
    lw      $8,  ctx_T0($sp)
    lw      $9,  ctx_T1($sp)
    lw      $10, ctx_T2($sp)
    lw      $11, ctx_T3($sp)
    lw      $12, ctx_T4($sp)
    lw      $13, ctx_T5($sp)
    lw      $14, ctx_T6($sp)
    lw      $15, ctx_T7($sp)
    lw      $16, ctx_S0($sp)
    lw      $17, ctx_S1($sp)
    lw      $18, ctx_S2($sp)
    lw      $19, ctx_S3($sp)
    lw      $20, ctx_S4($sp)
    lw      $21, ctx_S5($sp)
    lw      $22, ctx_S6($sp)
    lw      $23, ctx_S7($sp)
    lw      $24, ctx_T8($sp)
    lw      $25, ctx_T9($sp)
    lw      $28, ctx_GP($sp)
    lw      $30, ctx_FP($sp)
    lw      $31, ctx_RA($sp)

    addiu   $sp, $sp, portCONTEXT_BYTES   /* release frame */
    eret                                   /* clear EXL, jump to EPC */
.endm

/* =========================================================================
 * vPortISR — installed at EBase+0x200 (Cause.IV=1).
 *
 * Handles two interrupt sources:
 *   HW5 (IP7, Cause bit 15) — CP0 Count/Compare timer → scheduler tick
 *   SW0 (IP0, Cause bit 8)  — software yield request
 *
 * All registers are saved/restored around the C calls.
 * ========================================================================= */
    .globl  vPortISR
    .type   vPortISR, @function
    .align  2
vPortISR:
    portSAVE_CONTEXT

    /* ── Read Cause to identify interrupt source ── */
    mfc0    $t0, $13            /* CP0 Cause */

    /* ── Timer (HW5 = IP7 = Cause bit 15)? ── */
    li      $t1, (1 << 15)
    and     $t2, $t0, $t1
    beq     $t2, $zero, .Lcheck_sw0
     nop

    /* ── Timer interrupt: reprogram Compare, call FreeRTOS tick ── */
    mfc0    $t0, $9             /* Count */
    li      $t1, portCOMPARE_DELTA
    addu    $t0, $t0, $t1
    mtc0    $t0, $11            /* Compare */

    jal     xTaskIncrementTick
     nop

    /* If xTaskIncrementTick returned pdTRUE, switch context. */
    beq     $v0, $zero, .Ldone
     nop
    jal     vTaskSwitchContext
     nop
    b       .Ldone
     nop

    /* ── Software interrupt 0 (yield request) ── */
.Lcheck_sw0:
    li      $t1, (1 << 8)       /* SW0 bit in Cause */
    and     $t2, $t0, $t1
    beq     $t2, $zero, .Ldone
     nop

    /* Clear SW0 pending bit. */
    mfc0    $t0, $13
    li      $t1, ~(1 << 8)
    and     $t0, $t0, $t1
    mtc0    $t0, $13
    ehb

    jal     vTaskSwitchContext
     nop

.Ldone:
    portRESTORE_CONTEXT

    .size vPortISR, . - vPortISR


/* =========================================================================
 * _portRestoreContext — called by xPortStartScheduler() to load the first
 * task and start running.  Never returns.
 * ========================================================================= */
    .globl  _portRestoreContext
    .type   _portRestoreContext, @function
    .align  2
_portRestoreContext:
    portRESTORE_CONTEXT
    .size _portRestoreContext, . - _portRestoreContext
