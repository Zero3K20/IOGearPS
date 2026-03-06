/*
 * w25q_mt7688_flash.c — Winbond W25Q16JVSSIQ SPI NOR Flash driver for MT7688.
 *
 * eCos v2/v3 flash device driver for the 2 MB Winbond W25Q16JVSSIQ SPI NOR
 * flash fitted on the IOGear GPSU21 / ZOT Technology pu211 print server.
 *
 * ── Hardware overview ──────────────────────────────────────────────────────
 * SoC:       MediaTek MT7688 (MIPS32r2, little-endian)
 * Flash:     Winbond W25Q16JVSSIQ  (16 Mbit / 2 MB, 3.3 V SPI NOR)
 *              JEDEC ID:  0xEF 0x40 0x15
 *              Page size: 256 bytes
 *              Sector:    4 KB (erase unit, command 0x20)
 *
 * ── SPI controller ────────────────────────────────────────────────────────
 * The MT7688 integrates a "ralink,mt7621-spi"-compatible SPI master
 * (SPIMST) at physical 0x10000B00, KSEG1 alias 0xB0000B00.  CS0 of this
 * controller is wired to the W25Q16JVSSIQ.
 *
 * The controller is used in "MoreBuf" mode for all SPI NOR commands:
 *   SPI_OPCODE (0x04) — opcode byte (1 byte, CMD phase)
 *   SPI_DATA0..7 (0x08–0x24) — up to 32 bytes of MOSI payload (address +
 *                               data) or MISO result, little-endian packed
 *   SPI_MOREBUF (0x2C) — cmd_cnt[29:24] | miso_cnt[23:12] | mosi_cnt[11:0]
 *                         (all fields in BITS)
 *   SPI_POLAR   (0x38) — BIT(0) = assert CS0; 0 = deassert all CS
 *   SPI_TRANS   (0x00) — MSBFIRST|CLK_SEL|START; poll START to detect done
 *
 * ── Reads ─────────────────────────────────────────────────────────────────
 * Flash reads are served directly from the CPU's memory-mapped SPI NOR
 * window: physical 0x1C000000, KSEG1 0xBC000000.  The bootloader enables
 * this transparent read path before handing control to eCos.
 *
 * ── Writes / erases ───────────────────────────────────────────────────────
 * Each write or erase goes through the SPI controller:
 *   1. Write Enable  (0x06, 1-byte command)
 *   2. Sector Erase  (0x20 + 3-byte address) or
 *      Page Program  (0x02 + 3-byte address + ≤29 data bytes)
 *      — max 29 data bytes per SPI transaction: DATA0-7 = 32 bytes total,
 *        minus 3 address bytes leaves 29 bytes for payload.
 *   3. Poll Status Register-1 bit[0] (WIP) until zero.
 *
 * W25Q16JVSSIQ page-program stays within a 256-byte page boundary; the
 * driver splits writes that would cross a page boundary automatically.
 */

#include <pkgconf/system.h>
#include <pkgconf/devs_flash_winbond_w25q_mt7688.h>
#include <cyg/infra/cyg_type.h>
#include <cyg/io/flash.h>
#include <cyg/io/flash_dev.h>

/* ── MT7688 SPI controller register map (KSEG1, uncached) ────────────────── */

#define MT7688_SPI_BASE     0xB0000B00UL

#define SPI_TRANS           (MT7688_SPI_BASE + 0x00UL)
#define SPI_OPCODE          (MT7688_SPI_BASE + 0x04UL)
#define SPI_DATA0           (MT7688_SPI_BASE + 0x08UL)
/* SPI_DATA1 … SPI_DATA7 at 0x0C … 0x24 (4-byte stride) */
#define SPI_MOREBUF         (MT7688_SPI_BASE + 0x2CUL)
#define SPI_POLAR           (MT7688_SPI_BASE + 0x38UL)

/* SPI_TRANS bits */
#define SPITRANS_MSBFIRST   (1UL << 16)
#define SPITRANS_CLK_SEL(x) (((x) & 7UL) << 8)  /* 3 → ~24 MHz with 193 MHz bus */
#define SPITRANS_START      (1UL << 0)

/* SPI_MOREBUF fields (all in BITS) */
#define MOREBUF_CMD(n)      (((cyg_uint32)(n) & 0x3FUL) << 24)
#define MOREBUF_MISO(n)     (((cyg_uint32)(n) & 0xFFFUL) << 12)
#define MOREBUF_MOSI(n)     ((cyg_uint32)(n) & 0xFFFUL)

/* SPI_POLAR: BIT(cs) asserts chip-select; 0 deasserts all */
#define SPI_CS0_ASSERT      (1UL << 0)

/* ── W25Q16JVSSIQ SPI NOR commands ──────────────────────────────────────── */

#define W25Q_CMD_WREN       0x06U   /* Write Enable               */
#define W25Q_CMD_RDSR1      0x05U   /* Read Status Register-1     */
#define W25Q_CMD_SE         0x20U   /* Sector Erase (4 KB)        */
#define W25Q_CMD_PP         0x02U   /* Page Program               */
#define W25Q_CMD_RDID       0x9FU   /* Read JEDEC ID              */

#define W25Q_SR1_WIP        0x01U   /* Write In Progress bit      */

/* JEDEC ID bytes: manufacturer=0xEF, memory type=0x40, capacity=0x15 */
#define W25Q16_MFR          0xEFU
#define W25Q16_TYPE         0x40U
#define W25Q16_CAP          0x15U

/*
 * Maximum data bytes per Page Program SPI transaction.
 * DATA0..DATA7 = 8 registers × 4 bytes = 32 bytes total MOSI payload.
 * The 3-byte address is part of the MOSI payload, leaving 29 bytes for data.
 */
#define W25Q_PP_CHUNK       29U

/* W25Q16JVSSIQ page boundary (256 bytes) */
#define W25Q_PAGE_SIZE      256U

/* ── Low-level SPI register helpers ─────────────────────────────────────── */

static inline void spi_wr(cyg_uint32 addr, cyg_uint32 val)
{
    *((volatile cyg_uint32 *)addr) = val;
}

static inline cyg_uint32 spi_rd(cyg_uint32 addr)
{
    return *((volatile cyg_uint32 *)addr);
}

/* Poll SPI_TRANS until the START bit clears (transaction complete). */
static inline void spi_wait(void)
{
    while (spi_rd(SPI_TRANS) & SPITRANS_START)
        ;
}

/*
 * spi_pack_data() — load up to 32 bytes into DATA0..DATA7 (LE packed).
 *
 * The SPI controller sends/receives DATA registers little-endian within
 * each 32-bit word: byte[0] → DATA0[7:0], byte[1] → DATA0[15:8], etc.
 * Unused bytes are padded with 0xFF (erased-flash value).
 */
static void spi_pack_data(const cyg_uint8 *buf, cyg_uint32 n)
{
    cyg_uint32 i;
    for (i = 0; i < 8U; i++) {
        cyg_uint32 val = 0U;
        cyg_uint32 j;
        for (j = 0U; j < 4U; j++) {
            cyg_uint32 idx = i * 4U + j;
            val |= (idx < n) ? ((cyg_uint32)buf[idx] << (j * 8U))
                             : (0xFFUL << (j * 8U));
        }
        spi_wr(SPI_DATA0 + i * 4U, val);
    }
}

/*
 * spi_xfer() — execute one MoreBuf SPI transaction.
 *
 * @opcode     Command byte written to SPI_OPCODE.
 * @mosi_bits  MOSI bit count (payload in DATA0..DATA7, pre-loaded by caller).
 * @miso_bits  MISO bit count (result available in DATA0.. after return).
 *
 * The DATA0..DATA7 registers must be pre-loaded by the caller before this
 * function is called when mosi_bits > 0.  Results are in DATA0.. when
 * miso_bits > 0.
 */
static void spi_xfer(cyg_uint8 opcode, cyg_uint32 mosi_bits, cyg_uint32 miso_bits)
{
    spi_wr(SPI_OPCODE,  (cyg_uint32)opcode);
    spi_wr(SPI_MOREBUF, MOREBUF_CMD(8U) | MOREBUF_MISO(miso_bits) | MOREBUF_MOSI(mosi_bits));
    spi_wr(SPI_POLAR,   SPI_CS0_ASSERT);
    spi_wr(SPI_TRANS,   SPITRANS_MSBFIRST | SPITRANS_CLK_SEL(3U) | SPITRANS_START);
    spi_wait();
    spi_wr(SPI_POLAR, 0UL);
}

/* ── W25Q NOR command wrappers ───────────────────────────────────────────── */

static void w25q_write_enable(void)
{
    /* 0x06: 1-byte opcode only, no MOSI payload, no MISO */
    spi_xfer(W25Q_CMD_WREN, 0U, 0U);
}

static cyg_uint8 w25q_read_status1(void)
{
    /* 0x05: receive 1 byte (8 bits) of status into DATA0[7:0] */
    spi_xfer(W25Q_CMD_RDSR1, 0U, 8U);
    return (cyg_uint8)(spi_rd(SPI_DATA0) & 0xFFU);
}

/* Spin until the Write In Progress bit clears. */
static void w25q_wait_ready(void)
{
    while (w25q_read_status1() & W25Q_SR1_WIP)
        ;
}

/* ── eCos flash device driver callbacks ─────────────────────────────────── */

static int w25q_flash_init(struct cyg_flash_dev *dev)
{
    cyg_uint32 raw;
    cyg_uint8 mfr, type, cap;

    /*
     * Read JEDEC ID (0x9F): receive 3 bytes into DATA0.
     * Little-endian packing in DATA0:
     *   DATA0[7:0]  = byte 0 = manufacturer ID  (0xEF for Winbond)
     *   DATA0[15:8] = byte 1 = memory type      (0x40 for SPI NOR)
     *   DATA0[23:16]= byte 2 = capacity          (0x15 for 16 Mbit)
     */
    spi_xfer(W25Q_CMD_RDID, 0U, 24U);
    raw  = spi_rd(SPI_DATA0);
    mfr  = (cyg_uint8)((raw >>  0U) & 0xFFU);
    type = (cyg_uint8)((raw >>  8U) & 0xFFU);
    cap  = (cyg_uint8)((raw >> 16U) & 0xFFU);

    if (mfr != W25Q16_MFR || type != W25Q16_TYPE || cap != W25Q16_CAP)
        return CYG_FLASH_ERR_DRV_WRONG_PART;

    return CYG_FLASH_ERR_OK;
}

static size_t w25q_flash_query(struct cyg_flash_dev *dev, void *data, size_t len)
{
    /* Return the 3-byte JEDEC ID. */
    static const cyg_uint8 jedec[3] = { W25Q16_MFR, W25Q16_TYPE, W25Q16_CAP };
    size_t n = (len < sizeof(jedec)) ? len : sizeof(jedec);
    const cyg_uint8 *src = jedec;
    cyg_uint8 *dst = (cyg_uint8 *)data;
    size_t i;
    for (i = 0U; i < n; i++)
        dst[i] = src[i];
    return n;
}

static int w25q_flash_erase_block(struct cyg_flash_dev *dev,
                                   cyg_flashaddr_t block_base)
{
    /*
     * Sector Erase (4 KB, command 0x20).
     * Convert the KSEG1 virtual address to a 24-bit flash offset, then
     * build the MOSI payload: {addr[23:16], addr[15:8], addr[7:0]}.
     */
    cyg_uint32 base_va = (cyg_uint32)CYGNUM_DEVS_FLASH_MT7688_W25Q_FLASH_BASE;
    cyg_uint32 end_va  = base_va + (cyg_uint32)CYGNUM_DEVS_FLASH_MT7688_W25Q_FLASH_SIZE;
    cyg_uint32 blk     = (cyg_uint32)block_base;
    cyg_uint32 offset;

    if (blk < base_va || blk >= end_va)
        return CYG_FLASH_ERR_INVALID;

    offset = blk - base_va;
    cyg_uint8 mosi[3];
    mosi[0] = (cyg_uint8)((offset >> 16U) & 0xFFU);
    mosi[1] = (cyg_uint8)((offset >>  8U) & 0xFFU);
    mosi[2] = (cyg_uint8)( offset         & 0xFFU);

    spi_pack_data(mosi, 3U);
    w25q_write_enable();
    spi_xfer(W25Q_CMD_SE, 3U * 8U, 0U);   /* MOSI = 24 bits (3-byte address) */
    w25q_wait_ready();

    return CYG_FLASH_ERR_OK;
}

static int w25q_flash_program(struct cyg_flash_dev *dev,
                               cyg_flashaddr_t base,
                               const void *data,
                               size_t len)
{
    cyg_uint32 base_va = (cyg_uint32)CYGNUM_DEVS_FLASH_MT7688_W25Q_FLASH_BASE;
    cyg_uint32 end_va  = base_va + (cyg_uint32)CYGNUM_DEVS_FLASH_MT7688_W25Q_FLASH_SIZE;
    const cyg_uint8 *src  = (const cyg_uint8 *)data;
    cyg_uint32       addr;

    if ((cyg_uint32)base < base_va || (cyg_uint32)base >= end_va)
        return CYG_FLASH_ERR_INVALID;

    addr = (cyg_uint32)base - base_va;

    while (len > 0U) {
        /*
         * Determine bytes to program in this transaction:
         *   - At most W25Q_PP_CHUNK (29) bytes fit alongside the 3-byte
         *     address in DATA0..DATA7.
         *   - Must not cross a 256-byte page boundary.
         */
        cyg_uint32 page_rem = W25Q_PAGE_SIZE - (addr & (W25Q_PAGE_SIZE - 1U));
        cyg_uint32 chunk    = (cyg_uint32)len;
        cyg_uint8  mosi[3U + W25Q_PP_CHUNK];
        cyg_uint32 i;

        if (chunk > W25Q_PP_CHUNK)
            chunk = W25Q_PP_CHUNK;
        if (chunk > page_rem)
            chunk = page_rem;

        /*
         * Build the MOSI payload for Page Program (0x02):
         *   [0]    = addr[23:16]
         *   [1]    = addr[15:8]
         *   [2]    = addr[7:0]
         *   [3..N] = data bytes
         */
        mosi[0] = (cyg_uint8)((addr >> 16U) & 0xFFU);
        mosi[1] = (cyg_uint8)((addr >>  8U) & 0xFFU);
        mosi[2] = (cyg_uint8)( addr         & 0xFFU);
        for (i = 0U; i < chunk; i++)
            mosi[3U + i] = src[i];

        /*
         * Pre-load DATA registers before calling write_enable.
         * write_enable only touches SPI_OPCODE, SPI_MOREBUF, SPI_POLAR,
         * SPI_TRANS — DATA0..DATA7 are left intact.
         */
        spi_pack_data(mosi, 3U + chunk);
        w25q_write_enable();
        /* MOSI = (3 + chunk) bytes = address + data */
        spi_xfer(W25Q_CMD_PP, (3U + chunk) * 8U, 0U);
        w25q_wait_ready();

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
     * Flash reads use the CPU's memory-mapped SPI NOR window (KSEG1
     * 0xBC000000).  The base address passed in IS the KSEG1 virtual
     * address; a simple byte-copy suffices.
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
    /* Block protection not used; treat as no-op. */
    return CYG_FLASH_ERR_OK;
}

static int w25q_flash_block_unlock(struct cyg_flash_dev *dev,
                                    const cyg_flashaddr_t block_base)
{
    /* Block protection not used; treat as no-op. */
    return CYG_FLASH_ERR_OK;
}

/* ── Flash device function table ─────────────────────────────────────────── */

static const struct cyg_flash_dev_funs w25q_funs = {
    .flash_init         = w25q_flash_init,
    .flash_query        = w25q_flash_query,
    .flash_erase_block  = w25q_flash_erase_block,
    .flash_program      = w25q_flash_program,
    .flash_read         = w25q_flash_read,
    .flash_hwr_map_error= w25q_flash_hwr_map_error,
    .flash_block_lock   = w25q_flash_block_lock,
    .flash_block_unlock = w25q_flash_block_unlock,
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
    0,                                          /* flags        */
    CYGNUM_DEVS_FLASH_MT7688_W25Q_FLASH_BASE,   /* start        */
    CYGNUM_DEVS_FLASH_MT7688_W25Q_FLASH_BASE +
        CYGNUM_DEVS_FLASH_MT7688_W25Q_FLASH_SIZE - 1U, /* end   */
    1,                                          /* num_block_infos */
    w25q16_block_info,                          /* block_info   */
    NULL                                        /* priv         */
);
