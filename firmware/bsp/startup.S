/*
 * startup.S — MIPS32r2 firmware entry point for the IOGear GPSU21.
 *
 * The ZOT/U-Boot bootloader LZMA-decompresses the firmware image into KSEG0
 * RAM at 0x80500000 (taken from the uImage load_addr field, matching the OEM
 * eCos firmware) and branches here.  The bootloader has already configured:
 *   - SDRAM controller (64 MB at 0x80000000)
 *   - UART0 at 57600-8N1  (base KSEG1 0xB0000C00)
 *   - CPU clock (580 MHz), bus clock (193 MHz)
 *
 * Startup sequence
 * ────────────────
 *  1. Freeze CP0 Status: BEV=1 (bootstrap exception vectors), IE=0.
 *  2. Set up initial stack pointer (from linker symbol _stack_top).
 *  3. Zero the BSS segment.
 *  4. Call board_init() (C, in mt7688_init.c) which:
 *       a. copies exception-vector jump-stubs to KSEG0 0x80000000,
 *       b. sets Cause.IV=1 so interrupts use EBase+0x200,
 *       c. clears BEV so our vectors are live.
 *  5. Call main().
 *
 * Exception-vector strategy
 * ─────────────────────────
 * MIPS32 BEV=0 vectors are at EBase (default 0x80000000).  Our image starts
 * at 0x80500000; the preceding 5 MB (0x80000000–0x804FFFFF) is writable DRAM.
 * We write small trampolines there at run time (step 4 above) so the CPU can
 * reach our handlers via j instructions (both ends share the same 256 MB
 * KSEG0 region, so j-encoding is valid).
 */

    .set    noreorder
    .set    noat

/* ─────────────────────────────────────────────────────────────────────────
 * _start — firmware entry point at 0x80000400
 * ───────────────────────────────────────────────────────────────────────── */
    .section .text.startup, "ax", @progbits
    .globl  _start
    .type   _start, @function
    .align  2
_start:
    /* 1. Set CP0 Status: BEV=1 (use bootstrap vectors while we init),
     *    ERL=0, EXL=0, IE=0 (all interrupts disabled). */
    li      $t0, 0x00400000         /* BEV bit = bit 22 */
    mtc0    $t0, $12                /* CP0 Status */
    ehb

    /* 2. Set up initial stack pointer.
     *    _stack_top is defined in linker.ld; 8-byte aligned per MIPS EABI. */
    la      $sp, _stack_top
    li      $t0, ~7
    and     $sp, $sp, $t0

    /* 3. Zero BSS segment.
     *    __bss_start and __bss_end are defined in linker.ld. */
    la      $t0, __bss_start
    la      $t1, __bss_end
.Lbss_loop:
    beq     $t0, $t1, .Lbss_done
     nop
    sw      $zero, 0($t0)
    addiu   $t0, $t0, 4
    b       .Lbss_loop
     nop
.Lbss_done:

    /* 4. Board initialisation: install exception vectors, set Cause.IV,
     *    clear BEV → from this point our own exception handlers are live. */
    jal     board_init
     nop

    /* 5. Enter the application (creates FreeRTOS tasks, starts scheduler). */
    jal     main
     nop

    /* Should never return. */
.Lhalt:
    b       .Lhalt
     nop

    .size _start, . - _start
