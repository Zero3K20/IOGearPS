/*
 * w25q_mt7688_flash.c — eCos cyg_flash glue for the Winbond W25Q16JVSSIQ.
 *
 * This file is the thin eCos flash-framework integration layer for the
 * Winbond W25Q16JVSSIQ SPI NOR flash on the IOGear GPSU21 / ZOT Technology
 * pu211 (MediaTek MT7688 SoC).
 *
 * Hardware layer
 * ──────────────
 * All SPI NOR operations (JEDEC ID read, write enable, sector erase, page
 * program) are delegated to the W25Q hardware layer defined in w25_flash.h
 * and implemented in w25_flash.c.  That layer is based on the architecture
 * of DexterHaslem/winbond-w25-flash-drv (MIT):
 *   https://github.com/DexterHaslem/winbond-w25-flash-drv
 * The MT7688 implementation uses the SoC's MoreBuf SPI controller directly
 * rather than byte-by-byte SPI_EXCHANGE macros.
 *
 * Flash reads
 * ───────────
 * Reads use the CPU's memory-mapped SPI NOR window (KSEG1 0xBC000000) which
 * the bootloader enables before handing control to eCos — much faster than
 * going through the SPI controller for every byte.
 *
 * Write / erase sequence
 * ──────────────────────
 *   1. Validate address range.
 *   2. w25_flash_write_enable()  — assert WEL before every operation.
 *   3. w25_flash_sector_erase() or w25_flash_page_program()
 *      (page_program splits across the 29-byte MoreBuf limit and 256-byte
 *       W25Q page boundaries automatically).
 *   The w25_flash_* functions call w25_flash_wait_ready() internally.
 */

#include <pkgconf/system.h>
#include <pkgconf/devs_flash_winbond_w25q_mt7688.h>
#include <cyg/infra/cyg_type.h>
#include <cyg/io/flash.h>
#include <cyg/io/flash_dev.h>

#include "w25_flash.h"

/* Maximum data bytes per Page Program SPI transaction (MoreBuf DATA FIFO
 * is 32 bytes; 3 are consumed by the 24-bit address). */
#define W25Q_PP_CHUNK   29U

/* W25Q16JVSSIQ page size: program must not cross a 256-byte boundary. */
#define W25Q_PAGE_SIZE  256U

/* ── eCos flash device driver callbacks ─────────────────────────────────── */

static int w25q_flash_init(struct cyg_flash_dev *dev)
{
    struct w25_jedec_id jedec;

    w25_flash_read_jedec(&jedec);

    if (jedec.manuf_id != WINBOND_MANU_ID  ||
        jedec.mem_type != W25Q16_MEM_TYPE  ||
        jedec.capacity != W25Q16_CAPACITY)
        return CYG_FLASH_ERR_DRV_WRONG_PART;

    return CYG_FLASH_ERR_OK;
}

static size_t w25q_flash_query(struct cyg_flash_dev *dev,
                                void *data, size_t len)
{
    struct w25_jedec_id jedec;
    size_t n;
    cyg_uint8 *dst = (cyg_uint8 *)data;

    w25_flash_read_jedec(&jedec);
    n = (len < sizeof(jedec)) ? len : sizeof(jedec);
    dst[0] = jedec.manuf_id;
    if (n > 1U) dst[1] = jedec.mem_type;
    if (n > 2U) dst[2] = jedec.capacity;
    return n;
}

static int w25q_flash_erase_block(struct cyg_flash_dev *dev,
                                   cyg_flashaddr_t block_base)
{
    cyg_uint32 base_va = (cyg_uint32)CYGNUM_DEVS_FLASH_MT7688_W25Q_FLASH_BASE;
    cyg_uint32 end_va  = base_va
                       + (cyg_uint32)CYGNUM_DEVS_FLASH_MT7688_W25Q_FLASH_SIZE;
    cyg_uint32 blk     = (cyg_uint32)block_base;

    if (blk < base_va || blk >= end_va)
        return CYG_FLASH_ERR_INVALID;

    w25_flash_write_enable();
    w25_flash_sector_erase(blk - base_va);  /* pass flash offset, not VA */

    return CYG_FLASH_ERR_OK;
}

static int w25q_flash_program(struct cyg_flash_dev *dev,
                               cyg_flashaddr_t base,
                               const void *data,
                               size_t len)
{
    cyg_uint32       base_va = (cyg_uint32)CYGNUM_DEVS_FLASH_MT7688_W25Q_FLASH_BASE;
    cyg_uint32       end_va  = base_va
                             + (cyg_uint32)CYGNUM_DEVS_FLASH_MT7688_W25Q_FLASH_SIZE;
    const cyg_uint8 *src     = (const cyg_uint8 *)data;
    cyg_uint32       addr;

    if ((cyg_uint32)base < base_va || (cyg_uint32)base >= end_va)
        return CYG_FLASH_ERR_INVALID;

    addr = (cyg_uint32)base - base_va;      /* convert VA → flash offset */

    while (len > 0U) {
        /* Honour MoreBuf limit (29 bytes) and W25Q page boundary (256 B). */
        cyg_uint32 page_rem = W25Q_PAGE_SIZE - (addr & (W25Q_PAGE_SIZE - 1U));
        cyg_uint32 chunk    = (cyg_uint32)len;
        if (chunk > W25Q_PP_CHUNK) chunk = W25Q_PP_CHUNK;
        if (chunk > page_rem)      chunk = page_rem;

        w25_flash_write_enable();
        w25_flash_page_program(addr, src, chunk);

        src  += chunk;
        addr += chunk;
        len  -= (size_t)chunk;
    }

    return CYG_FLASH_ERR_OK;
}

static int w25q_flash_read(struct cyg_flash_dev *dev,
                            const cyg_flashaddr_t base,
                            void *data,
                            size_t len)
{
    /*
     * Use the CPU's memory-mapped SPI NOR window (KSEG1 0xBC000000).
     * The base address IS the KSEG1 virtual address; a volatile byte-copy
     * forces uncached accesses through the SPI-NOR read path.
     */
    const volatile cyg_uint8 *src = (const volatile cyg_uint8 *)base;
    cyg_uint8 *dst = (cyg_uint8 *)data;
    size_t i;
    for (i = 0U; i < len; i++)
        dst[i] = src[i];
    return CYG_FLASH_ERR_OK;
}

static int w25q_flash_hwr_map_error(struct cyg_flash_dev *dev, int err)
{
    return err;
}

static int w25q_flash_block_lock(struct cyg_flash_dev *dev,
                                  const cyg_flashaddr_t block_base)
{
    return CYG_FLASH_ERR_OK;    /* block protection not used */
}

static int w25q_flash_block_unlock(struct cyg_flash_dev *dev,
                                    const cyg_flashaddr_t block_base)
{
    return CYG_FLASH_ERR_OK;    /* block protection not used */
}

/* ── Flash device function table ─────────────────────────────────────────── */

static const struct cyg_flash_dev_funs w25q_funs = {
    .flash_init          = w25q_flash_init,
    .flash_query         = w25q_flash_query,
    .flash_erase_block   = w25q_flash_erase_block,
    .flash_program       = w25q_flash_program,
    .flash_read          = w25q_flash_read,
    .flash_hwr_map_error = w25q_flash_hwr_map_error,
    .flash_block_lock    = w25q_flash_block_lock,
    .flash_block_unlock  = w25q_flash_block_unlock,
};

/* 512 sectors × 4 KB = 2 MB */
static const cyg_flash_block_info_t w25q16_block_info[] = {
    {
        CYGNUM_DEVS_FLASH_MT7688_W25Q_SECTOR_SIZE,
        CYGNUM_DEVS_FLASH_MT7688_W25Q_FLASH_SIZE /
            CYGNUM_DEVS_FLASH_MT7688_W25Q_SECTOR_SIZE
    }
};

/*
 * Register this device with the eCos flash framework.
 * start/end are the KSEG1 virtual addresses of the 2 MB flash window.
 */
CYG_FLASH_DRIVER(w25q_mt7688_flash_dev,
    &w25q_funs,
    0,                                              /* flags           */
    CYGNUM_DEVS_FLASH_MT7688_W25Q_FLASH_BASE,       /* start           */
    CYGNUM_DEVS_FLASH_MT7688_W25Q_FLASH_BASE +
        CYGNUM_DEVS_FLASH_MT7688_W25Q_FLASH_SIZE - 1U, /* end          */
    1,                                              /* num_block_infos */
    w25q16_block_info,                              /* block_info      */
    NULL                                            /* priv            */
);
