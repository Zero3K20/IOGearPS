/*
 * plf_io.h — Platform I/O definitions for the MediaTek MT7688 SoC.
 *
 * This header is referenced by CYGBLD_HAL_PLATFORM_H (defined in the MT7688
 * platform HAL CDL and written to pkgconf/system.h).  It is included early
 * in the include chain by pkgconf/hal.h via "#include CYGBLD_HAL_PLATFORM_H".
 *
 * Only SoC base-address macros are defined here.  Higher-level I/O access
 * macros (HAL_READ_UINT32 / HAL_WRITE_UINT32) are provided by the MIPS
 * architecture HAL (CYGPKG_HAL_MIPS) and must not be included from here to
 * avoid circular include chains during eCos kernel header generation.
 */

#ifndef CYGONCE_HAL_PLF_IO_H
#define CYGONCE_HAL_PLF_IO_H

/* ── MT7688 SoC peripheral base addresses (KSEG1 uncached) ─────────────── */

/* System Controller */
#define MT7688_SYSCTL_BASE      0xB0000000UL

/* UART0 (debug console, 57600-8N1) */
#define MT7688_UART0_BASE       0xB0000C00UL

/* SPI controller */
#define MT7688_SPI_BASE         0xB0000B00UL

/* Ethernet (RAETH) */
#define MT7688_ETH_BASE         0xB0100000UL

/* USB host controller (EHCI) */
#define MT7688_USBHOST_BASE     0xB0120000UL

/* GPIO */
#define MT7688_GPIO_BASE        0xB0000600UL

/* ── Interrupt controller ────────────────────────────────────────────────
 * The MT7688 uses the standard MIPS32 hardware interrupt lines (HW0–HW5)
 * mapped to the MIPS CP0 Cause register.  SoC-level interrupt dispatch is
 * handled in mt7688_startup.c using the eCos MIPS32 architecture HAL.
 * ──────────────────────────────────────────────────────────────────────── */

#endif /* CYGONCE_HAL_PLF_IO_H */
