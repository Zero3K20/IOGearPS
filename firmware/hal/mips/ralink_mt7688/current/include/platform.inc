#ifndef CYGONCE_HAL_PLATFORM_INC
#define CYGONCE_HAL_PLATFORM_INC
##=============================================================================
##
##	platform.inc
##
##	MediaTek MT7688 MIPS32r2 SoC — platform assembler header for eCos
##
##=============================================================================
## This file is part of eCos, the Embedded Configurable Operating System.
## Copyright (C) 1998, 1999, 2000, 2001, 2002 Free Software Foundation, Inc.
##
## eCos is free software; you can redistribute it and/or modify it under
## the terms of the GNU General Public License as published by the Free
## Software Foundation; either version 2 or (at your option) any later
## version.
##
## eCos is distributed in the hope that it will be useful, but WITHOUT
## ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
## FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
## for more details.
##
## You should have received a copy of the GNU General Public License
## along with eCos; if not, write to the Free Software Foundation, Inc.,
## 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
##=============================================================================
##
## Purpose:   MT7688 platform assembler definitions.
##
## Description:
##   Platform-specific assembler macros used by the MIPS architecture HAL
##   (cyg/hal/arch.inc) and the MIPS32 variant HAL (cyg/hal/variant.inc)
##   when building context.S and vectors.S for the IOGear GPSU21 / ZOT
##   pu211 print server.
##
##   The MT7688 is a MIPS32r2 little-endian SoC from Ralink / MediaTek.
##   eCos always starts from DRAM (RAM startup only): U-Boot has already
##   initialised SDRAM, clocks and UART0 before jumping to the eCos image.
##
## Usage:
##   #include <cyg/hal/platform.inc>
##
##=============================================================================

#include <cyg/hal/mips.inc>

#include <cyg/hal/hal_arch.h>
#include <cyg/hal/plf_io.h>

##-----------------------------------------------------------------------------
## CP0 Status register: additional platform bits.
##
## Set bits 15:10 to enable hardware interrupt lines HW0–HW5.  This value
## is OR'd into INITIAL_SR in the MIPS arch HAL (vectors.S) when the CP0
## Status register is first written.

#define INITIAL_SR_PLF  0x0000fc00

##-----------------------------------------------------------------------------
## Reset-vector first code (RAM startup — nothing to do here).

#define CYGPKG_HAL_RESET_VECTOR_FIRST_CODE
	.macro	hal_reset_vector_first_code
	.endm

##-----------------------------------------------------------------------------
## Early platform initialisation (RAM startup — U-Boot has done everything).

#define CYGPKG_HAL_EARLY_INIT
	.macro	hal_early_init
	.endm

##-----------------------------------------------------------------------------
## Interrupt controller macros.
##
## The MT7688 exposes six standard MIPS32 hardware interrupt lines (HW0–HW5)
## via CP0 Cause bits IP[2:7] (register bits 15:10).  There is no additional
## SoC-level interrupt aggregation at this HAL layer.

#ifndef CYGPKG_HAL_MIPS_INTC_INIT_DEFINED
	.macro	hal_intc_init
	mfc0	v0, status
	nop
	ori	v0, v0, 0xfc00		# enable HW0–HW5
	mtc0	v0, status
	nop
	nop
	nop
	.endm
#define CYGPKG_HAL_MIPS_INTC_INIT_DEFINED
#endif

#ifndef CYGPKG_HAL_MIPS_INTC_DECODE_DEFINED
	## Decode the highest-priority pending hardware interrupt.
	## Result placed in \vnum:
	##   0 = HW0 (CP0 Cause bit 10) … 5 = HW5 (CP0 Cause bit 15).
	.macro	hal_intc_decode	vnum
	mfc0	a1, status		# read interrupt enable mask
	mfc0	a0, cause		# read pending interrupts
	andi	a1, a1, 0xfc00		# isolate HW0–HW5 mask bits
	and	a1, a1, a0		# pending & enabled
	beq	a1, zero, 1f		# nothing pending
	li	\vnum, CYGNUM_HAL_INTERRUPT_BOGUS
	clz	a0, a1			# count leading zeros
	li	a1, 21
	subu	\vnum, a1, a0		# bit pos: 0=HW0 … 5=HW5
1:
	.endm
#define CYGPKG_HAL_MIPS_INTC_DECODE_DEFINED
#endif

#ifndef CYGPKG_HAL_MIPS_INTC_TRANSLATE_DEFINED
	.macro	hal_intc_translate	inum, vnum
	move	\vnum, \inum		# 1:1 mapping
	.endm
#define CYGPKG_HAL_MIPS_INTC_TRANSLATE_DEFINED
#endif

	.macro	hal_intc_decode_data
	.endm

#define CYGPKG_HAL_MIPS_INTC_DEFINED

##-----------------------------------------------------------------------------
## Diagnostic macros (UART0 I/O is handled in C by mt7688_startup.c).

#ifndef CYGPKG_HAL_MIPS_DIAG_DEFINED
	.macro	hal_diag_init
	.endm

	.macro	hal_diag_excpt_start
	.endm

	.macro	hal_diag_intr_start
	.endm

	.macro	hal_diag_restore
	.endm

#define CYGPKG_HAL_MIPS_DIAG_DEFINED
#endif

##-----------------------------------------------------------------------------
## Memory controller (MEMC) macros.
## RAM startup only — U-Boot has already initialised SDRAM.

#if defined(CYG_HAL_STARTUP_ROM)
#error "ROM startup is not supported on the MT7688 platform."
#endif

## Cache geometry: the MT7688 has 32-byte cache lines.
#define MT7688_DCACHE_LINE_SIZE  0x20
#define MT7688_ICACHE_LINE_SIZE  0x20

##-----------------------------------------------------------------------------
## Exception / interrupt vector copy.
##
## For RAM startup without a ROM monitor, copy the eCos exception vectors
## into their fixed MIPS32 locations in KSEG0.

#if !defined(CYGSEM_HAL_USE_ROM_MONITOR)
	.macro	hal_vectors_init
	.set	push
	.set	noreorder

	# Copy TLB-miss (utlb) vector → 0x80000000
	la	t0, CYGARC_KSEG_CACHED_BASE
	la	t1, utlb_vector
	la	t3, utlb_vector_end
1:
	lw	v0, 0(t1)
	addi	t1, t1, 4
	sw	v0, 0(t0)
	bne	t1, t3, 1b
	 addi	t0, t0, 4

	# Copy general exception vector → 0x80000180
	la	t0, CYGARC_KSEG_CACHED_BASE + 0x180
	la	t1, other_vector
	la	t3, other_vector_end
1:
	lw	v0, 0(t1)
	addi	t1, t1, 4
	sw	v0, 0(t0)
	bne	t1, t3, 1b
	 addi	t0, t0, 4

	# Flush the written region from D- and I-caches
	la	t0, CYGARC_KSEG_CACHED_BASE
	ori	t1, t0, 0x200
	.set	mips32r2
1:
	cache	0x15, 0(t0)		# writeback-invalidate D-cache line
	cache	0x10, 0(t0)		# invalidate I-cache line
	addiu	t0, t0, MT7688_DCACHE_LINE_SIZE
	bne	t0, t1, 1b
	 nop
	.set	pop
	.endm
#else
	.macro	hal_vectors_init
	.endm
#endif

##-----------------------------------------------------------------------------
## Monitor / VSR table initialisation.

#ifndef CYGPKG_HAL_MIPS_MON_DEFINED

#if defined(CYG_HAL_STARTUP_RAM) && !defined(CYGSEM_HAL_USE_ROM_MONITOR)

	.macro	hal_mon_init
	hal_vectors_init
	# Install default exception VSR in all basic exception slots (0–15)
	ori	a0, zero, 16		# basic exception vector count
	la	a1, __default_exception_vsr
	la	a2, hal_vsr_table
1:
	sw	a1, 0(a2)
	addi	a2, a2, 4
	addi	a0, a0, -1
	bne	a0, zero, 1b
	 nop
	# Install interrupt VSR (vector 0)
	la	a0, hal_vsr_table
	la	a1, __default_interrupt_vsr
	sw	a1, 0*4(a0)		# CYGNUM_HAL_VECTOR_INTERRUPT
	# Install breakpoint springboard (vector 9)
	la	a1, __break_vsr_springboard
	sw	a1, 9*4(a0)		# CYGNUM_HAL_VECTOR_BREAKPOINT
	# Special-purpose exception vectors
	la	a1, __default_exception_vsr
	sw	a1, 32*4(a0)		# debug
	sw	a1, 33*4(a0)		# utlb
	sw	a1, 34*4(a0)		# nmi
	.endm

#elif defined(CYG_HAL_STARTUP_RAM) && defined(CYGSEM_HAL_USE_ROM_MONITOR)

	.macro	hal_mon_init
	hal_vectors_init
	la	a0, __default_interrupt_vsr
	la	a3, hal_vsr_table
	sw	a0, 0(a3)
	.endm

#else

	.macro	hal_mon_init
	hal_vectors_init
	.endm

#endif

#define CYGPKG_HAL_MIPS_MON_DEFINED
#endif

##-----------------------------------------------------------------------------
#endif /* CYGONCE_HAL_PLATFORM_INC */
## end of platform.inc
