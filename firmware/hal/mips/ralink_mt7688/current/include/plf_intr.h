#ifndef CYGONCE_HAL_PLF_INTR_H
#define CYGONCE_HAL_PLF_INTR_H

/*
 * plf_intr.h — Platform interrupt definitions for the MediaTek MT7688 SoC.
 *
 * The MT7688 uses the standard MIPS32 hardware interrupt lines (HW0–HW5)
 * routed through the CP0 Cause register IP[2:7].  All interrupt vector
 * numbers and the HAL_INTERRUPT_MASK / HAL_INTERRUPT_UNMASK /
 * HAL_INTERRUPT_ACKNOWLEDGE macros are provided by the MIPS architecture
 * HAL (cyg/hal/hal_intr.h) after this header is included; no platform-
 * specific overrides are needed.
 *
 * This header is included by the MIPS32 variant interrupt header
 * (cyg/hal/var_intr.h, installed from
 * ecos-3.0/packages/hal/mips/mips32/v3_0/include/var_intr.h) which itself
 * is included by the architecture hal_intr.h.  Every eCos MIPS32 platform
 * HAL must supply this file.
 */

#include <pkgconf/hal.h>

#ifndef __ASSEMBLER__
#include <cyg/infra/cyg_type.h>
#endif

/*
 * Platform reset.
 *
 * The MT7688 System Controller exposes a software-reset register at
 * offset 0x34 of the SYSCTL block (KSEG1 uncached address 0xB0000034).
 * Writing bit 0 triggers an immediate SoC-wide reset.
 *
 * HAL_PLATFORM_RESET_ENTRY is the MIPS standard reset vector (the
 * beginning of the BootROM / U-Boot in KSEG1).
 */
#ifndef CYGHWR_HAL_RESET_DEFINED

#ifndef __ASSEMBLER__
/* MT7688 System Controller: software reset register (KSEG1 uncached) */
#define MT7688_SYSCTL_RSTCTRL_REG   0xB0000034UL

static inline void hal_mt7688_reset(void)
{
    volatile unsigned int *rstctrl =
        (volatile unsigned int *)MT7688_SYSCTL_RSTCTRL_REG;
    *rstctrl = 1U;
    for (;;)
        ;   /* wait for reset to take effect */
}
#endif /* __ASSEMBLER__ */

#define HAL_PLATFORM_RESET()        hal_mt7688_reset()
#define HAL_PLATFORM_RESET_ENTRY    0xBFC00000UL
#define CYGHWR_HAL_RESET_DEFINED

#endif /* CYGHWR_HAL_RESET_DEFINED */

#endif /* CYGONCE_HAL_PLF_INTR_H */
/* End of plf_intr.h */
