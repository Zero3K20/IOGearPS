/*
 * cc.h — lwIP architecture/portability header for MIPS32r2 little-endian
 *         (MediaTek MT7688) running FreeRTOS.
 *
 * Included by lwip/arch.h via the LWIP_ARCH_CC_H discovery path.
 */

#ifndef GPSU21_LWIP_ARCH_CC_H
#define GPSU21_LWIP_ARCH_CC_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ── Byte-order ─────────────────────────────────────────────────────────── */
/* MT7688 is little-endian. Guard against redefinition from system headers. */
#ifndef BYTE_ORDER
#define BYTE_ORDER  LITTLE_ENDIAN
#endif

/* ── Primitive types ────────────────────────────────────────────────────── */
typedef uint8_t     u8_t;
typedef int8_t      s8_t;
typedef uint16_t    u16_t;
typedef int16_t     s16_t;
typedef uint32_t    u32_t;
typedef int32_t     s32_t;
typedef uintptr_t   mem_ptr_t;

/* ── printf format strings ──────────────────────────────────────────────── */
#define U16_F   "u"
#define S16_F   "d"
#define X16_F   "x"
#define U32_F   "u"
#define S32_F   "d"
#define X32_F   "x"
#define SZT_F   "u"

/* ── Compiler hints ─────────────────────────────────────────────────────── */
#define PACK_STRUCT_FIELD(x)    x
#define PACK_STRUCT_STRUCT      __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

/* ── Platform diagnostics ───────────────────────────────────────────────── */
#define LWIP_PLATFORM_DIAG(x)   do { printf x; } while (0)
/*
 * LWIP_PLATFORM_ASSERT — called when lwIP detects an internal invariant
 * violation.  Forward to vAssertCalled() (defined in firmware/src/main.c)
 * so the device resets and can be reflashed rather than hanging forever.
 */
extern __attribute__((noreturn)) void vAssertCalled(void);
#define LWIP_PLATFORM_ASSERT(x) do { printf("LWIP ASSERT: %s\n", (x)); vAssertCalled(); } while(0)

/* ── Random number (simple LCG for mDNS / ARP) ─────────────────────────── */
#define LWIP_RAND()  ((u32_t)rand())

#endif /* LWIP_ARCH_CC_H */
