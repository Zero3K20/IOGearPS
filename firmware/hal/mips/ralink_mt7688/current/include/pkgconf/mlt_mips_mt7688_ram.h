/*
 * mlt_mips_mt7688_ram.h — Memory layout header for the MT7688 (RAM startup).
 *
 * The eCos application is loaded by the ZOT bootloader into KSEG0 DRAM
 * at physical address 0x00000400 (KSEG0 virtual: 0x80000400).  The first
 * 1 KiB (0x000–0x3FF) is reserved for MIPS exception vectors which are
 * kept in the bootloader image.
 *
 * MT7688 SDRAM: 64 MB (0x04000000 bytes) at physical 0x00000000.
 *   KSEG0 uncached alias: 0x80000000
 *   eCos load offset:     0x80000400  (skip 1 KB for MIPS vectors)
 *   Usable region:        0x80000400 – 0x83FFFFFF  (~63.999 MB)
 */

#ifndef CYGONCE_MLT_MIPS_MT7688_RAM_H
#define CYGONCE_MLT_MIPS_MT7688_RAM_H

/* ── RAM region ─────────────────────────────────────────────────────────── */

/* eCos image load base (skip MIPS exception-vector page at 0x80000000) */
#define CYGMEM_REGION_ram        (0x80000400)

/* 64 MB total SDRAM (0x04000000) minus the 1 KB reserved for MIPS vectors
 * (0x400), minus 64 bytes alignment rounding = 0x04000000 - 0x400        */
#define CYGMEM_REGION_ram_SIZE   (0x04000000 - 0x400)

/* Read-write (no execute-bit enforcement — MIPS32 does not have NX per-page) */
#define CYGMEM_REGION_ram_ATTR   (CYGMEM_REGION_ATTR_R | CYGMEM_REGION_ATTR_W)

/* ── Heap ───────────────────────────────────────────────────────────────── */

#ifndef __ASSEMBLER__
extern char CYG_LABEL_DEFN(__heap1)[];
#define CYGMEM_SECTION_heap1 (CYG_LABEL_DEFN(__heap1))
#define CYGMEM_SECTION_heap1_SIZE \
    ((CYGMEM_REGION_ram + CYGMEM_REGION_ram_SIZE) \
     - (size_t)CYG_LABEL_DEFN(__heap1))
#endif /* __ASSEMBLER__ */

#endif /* CYGONCE_MLT_MIPS_MT7688_RAM_H */
