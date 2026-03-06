#ifndef CYGONCE_HAL_PLF_CACHE_H
#define CYGONCE_HAL_PLF_CACHE_H

/*
 * plf_cache.h — Platform cache definitions for the MediaTek MT7688 SoC.
 *
 * This header is included by the MIPS32 variant cache header
 * (cyg/hal/var_cache.h, installed from
 * ecos-3.0/packages/hal/mips/mips32/v3_0/include/var_cache.h) which itself
 * is included by the architecture hal_cache.h.  Every eCos MIPS32 platform
 * HAL must supply this file.
 *
 * The MT7688 uses a standard MIPS32r2 Harvard cache (I-cache and D-cache)
 * whose geometry is detected at runtime by the architecture HAL using the
 * MIPS32 Config1 CP0 register.  No platform-specific cache overrides are
 * required; all cache flush / invalidate operations are handled by the
 * generic MIPS32 variant implementation.
 */

#endif /* CYGONCE_HAL_PLF_CACHE_H */
/* End of plf_cache.h */
