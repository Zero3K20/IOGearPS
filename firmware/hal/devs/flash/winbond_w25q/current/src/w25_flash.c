/*
 * w25_flash.c — MT7688 MoreBuf SPI implementation of the W25Q flash API.
 *
 * Implements the hardware layer declared in w25_flash.h for the MediaTek
 * MT7688 SoC using its built-in MoreBuf SPI master controller (SPIMST,
 * "ralink,mt7621-spi" compatible, KSEG1 base 0xB0000B00).
 *
 * Architecture
 * ────────────
 * DexterHaslem/winbond-w25-flash-drv (MIT) defines the same API and uses a
 * byte-by-byte SPI_EXCHANGE() abstraction well-suited for generic SPI
 * peripherals.  The MT7688 MoreBuf controller is more efficient when whole
 * flash commands are assembled and dispatched as a single transaction (opcode
 * + address + data in one MoreBuf frame), so this file implements the same
 * API using that batching approach rather than byte-by-byte exchange.
 *
 * MoreBuf register map (KSEG1 uncached, base 0xB0000B00)
 * ─────────────────────────────────────────────────────
 *  0x00  SPI_TRANS   — [16] MSBFIRST  [10:8] CLK_SEL  [0] START
 *  0x04  SPI_OPCODE  — opcode byte sent in CMD phase
 *  0x08  SPI_DATA0   — DATA0..DATA7 (0x08–0x24), 4-byte stride, LE packed
 *  0x2C  SPI_MOREBUF — [29:24] cmd_cnt(bits) | [23:12] miso_cnt(bits)
 *                                            | [11:0]  mosi_cnt(bits)
 *  0x38  SPI_POLAR   — BIT(0)=assert CS0; 0=deassert all CS
 *
 * Transaction sequence for each SPI NOR command:
 *   1. Load opcode into SPI_OPCODE.
 *   2. Load address + data (if any) into DATA0..DATA7, little-endian.
 *   3. Write SPI_MOREBUF: CMD=8 | MISO=<rx_bits> | MOSI=<tx_bits>.
 *   4. Assert CS: SPI_POLAR = 1.
 *   5. Start: SPI_TRANS = MSBFIRST | CLK_SEL(3) | START.
 *   6. Spin on SPI_TRANS[0] until START clears.
 *   7. Deassert CS: SPI_POLAR = 0.
 *   8. Read result from DATA0 (for read operations).
 */

#include "w25_flash.h"
#include <cyg/infra/cyg_type.h>

/* ── MT7688 SPI controller registers (KSEG1 = uncached) ─────────────────── */

#define MT7688_SPI_BASE     0xB0000B00UL

#define SPI_TRANS           (MT7688_SPI_BASE + 0x00UL)
#define SPI_OPCODE          (MT7688_SPI_BASE + 0x04UL)
#define SPI_DATA0           (MT7688_SPI_BASE + 0x08UL)
#define SPI_MOREBUF         (MT7688_SPI_BASE + 0x2CUL)
#define SPI_POLAR           (MT7688_SPI_BASE + 0x38UL)

#define SPITRANS_MSBFIRST   (1UL << 16)
#define SPITRANS_CLK_SEL(x) (((x) & 7UL) << 8)   /* 3 → ~24 MHz at 193 MHz bus */
#define SPITRANS_START      (1UL << 0)

/* MOREBUF field packing — all sizes in BITS */
#define MOREBUF_CMD(n)  (((cyg_uint32)(n) & 0x3FUL) << 24)
#define MOREBUF_MISO(n) (((cyg_uint32)(n) & 0xFFFUL) << 12)
#define MOREBUF_MOSI(n) ((cyg_uint32)(n) & 0xFFFUL)

#define SPI_CS0_ASSERT      (1UL << 0)

/* ── Private helpers ─────────────────────────────────────────────────────── */

static inline void spi_wr(cyg_uint32 addr, cyg_uint32 val)
{
    *((volatile cyg_uint32 *)addr) = val;
}

static inline cyg_uint32 spi_rd(cyg_uint32 addr)
{
    return *((volatile cyg_uint32 *)addr);
}

static inline void spi_wait(void)
{
    while (spi_rd(SPI_TRANS) & SPITRANS_START)
        ;
}

/*
 * spi_xfer() — execute one MoreBuf SPI transaction with CS toggling.
 *
 * @opcode     Command byte (sent in the 8-bit CMD phase).
 * @mosi_bits  Number of MOSI payload bits; DATA0..DATA7 must be pre-loaded.
 * @miso_bits  Number of MISO bits to receive; result placed in DATA0..DATA7.
 */
static void spi_xfer(cyg_uint8 opcode,
                     cyg_uint32 mosi_bits,
                     cyg_uint32 miso_bits)
{
    spi_wr(SPI_OPCODE,  (cyg_uint32)opcode);
    spi_wr(SPI_MOREBUF, MOREBUF_CMD(8U) | MOREBUF_MISO(miso_bits)
                                        | MOREBUF_MOSI(mosi_bits));
    spi_wr(SPI_POLAR,   SPI_CS0_ASSERT);
    spi_wr(SPI_TRANS,   SPITRANS_MSBFIRST | SPITRANS_CLK_SEL(3U)
                                          | SPITRANS_START);
    spi_wait();
    spi_wr(SPI_POLAR, 0UL);
}

/*
 * spi_pack_data() — pack up to 32 bytes into DATA0..DATA7 (LE byte order).
 *
 * The MoreBuf controller serialises DATA0..DATA7 MSB-first within each
 * 32-bit word, but the bytes within each word are stored in little-endian
 * order: byte[0] → DATA0[7:0], byte[1] → DATA0[15:8], etc.  Unused bytes
 * are padded with 0xFF (erased-flash value).
 */
static void spi_pack_data(const cyg_uint8 *buf, cyg_uint32 n)
{
    cyg_uint32 i;
    for (i = 0U; i < 8U; i++) {
        cyg_uint32 val = 0U;
        cyg_uint32 j;
        for (j = 0U; j < 4U; j++) {
            cyg_uint32 idx = i * 4U + j;
            val |= (idx < n)
                    ? ((cyg_uint32)buf[idx] << (j * 8U))
                    : (0xFFUL       << (j * 8U));
        }
        spi_wr(SPI_DATA0 + i * 4U, val);
    }
}

/*
 * spi_unpack_data() — unpack up to 32 bytes from DATA0..DATA7 into @buf.
 */
static void spi_unpack_data(cyg_uint8 *buf, cyg_uint32 n)
{
    cyg_uint32 i;
    for (i = 0U; i < n; i++) {
        cyg_uint32 word = spi_rd(SPI_DATA0 + (i / 4U) * 4U);
        buf[i] = (cyg_uint8)((word >> ((i % 4U) * 8U)) & 0xFFU);
    }
}

/*
 * spi_send_24bit_addr() — build a 3-byte big-endian flash address in the
 * first 3 bytes of the DATA0..DATA7 region, then pack @data_len additional
 * bytes from @data after the address.
 *
 * For MOSI the MoreBuf controller clocks DATA0 MSB first per byte (little-
 * endian word storage, big-endian bit order), which matches the SPI NOR
 * flash address byte order (addr[23:16] first).
 */
static void spi_load_addr_data(cyg_uint32 addr,
                                const cyg_uint8 *data,
                                cyg_uint32 data_len)
{
    /* Scratch buffer: 3 address bytes + up to 29 data bytes = 32 bytes max */
    cyg_uint8 buf[32];
    cyg_uint32 i;

    buf[0] = (cyg_uint8)((addr >> 16U) & 0xFFU);
    buf[1] = (cyg_uint8)((addr >>  8U) & 0xFFU);
    buf[2] = (cyg_uint8)( addr         & 0xFFU);
    for (i = 0U; i < data_len; i++)
        buf[3U + i] = data[i];

    spi_pack_data(buf, 3U + data_len);
}

/*
 * spi_pack_addr() — convenience helper: pack a 24-bit flash address into
 * the first 3 bytes of the DATA0..DATA7 region (big-endian byte order).
 */
static void spi_pack_addr(cyg_uint32 addr)
{
    cyg_uint8 a[3];
    a[0] = (cyg_uint8)((addr >> 16U) & 0xFFU);
    a[1] = (cyg_uint8)((addr >>  8U) & 0xFFU);
    a[2] = (cyg_uint8)( addr         & 0xFFU);
    spi_pack_data(a, 3U);
}

/* ── Public W25Q flash API (declared in w25_flash.h) ────────────────────── */

void w25_flash_read_jedec(struct w25_jedec_id *jedec)
{
    /*
     * READ JEDEC ID (0x9F): CMD=8 bits, MOSI=0, MISO=24 bits.
     * The flash returns 3 bytes: manufacturer ID, memory type, capacity.
     * MoreBuf places them in DATA0[23:0] (byte 0 in DATA0[7:0] etc.).
     */
    spi_xfer(W25Q_OP_READ_JEDEC_ID, 0U, 24U);
    spi_unpack_data((cyg_uint8 *)jedec, 3U);
}

cyg_uint8 w25_flash_read_status1(void)
{
    /* READ STATUS REGISTER 1 (0x05): CMD=8, MOSI=0, MISO=8 */
    spi_xfer(W25Q_OP_READ_STATUS_REG_1, 0U, 8U);
    return (cyg_uint8)(spi_rd(SPI_DATA0) & 0xFFU);
}

void w25_flash_wait_ready(void)
{
    while (w25_flash_read_status1() & W25Q_SR1_BUSY)
        ;
}

void w25_flash_write_enable(void)
{
    /* WRITE ENABLE (0x06): CMD=8, no MOSI, no MISO */
    spi_xfer(W25Q_OP_WRITE_ENABLE, 0U, 0U);
}

void w25_flash_write_disable(void)
{
    /* WRITE DISABLE (0x04): CMD=8, no MOSI, no MISO */
    spi_xfer(W25Q_OP_WRITE_DISABLE, 0U, 0U);
}

void w25_flash_sector_erase(cyg_uint32 addr)
{
    /*
     * SECTOR ERASE 4 KB (0x20): CMD=8, MOSI=24 (3-byte address), MISO=0.
     * Caller must issue write_enable() first.
     * Blocks until the erase completes.
     */
    spi_pack_addr(addr);
    spi_xfer(W25Q_OP_SECTOR_ERASE, 24U, 0U);
    w25_flash_wait_ready();
}

void w25_flash_page_program(cyg_uint32 addr,
                             const cyg_uint8 *buf,
                             cyg_uint32 count)
{
    /*
     * PAGE PROGRAM (0x02):
     *   CMD=8, MOSI = (3 + count) × 8 bits, MISO=0.
     *
     * The MoreBuf DATA FIFO is 32 bytes.  3 bytes are consumed by the
     * 24-bit address, leaving 29 bytes for data.  The caller is responsible
     * for not exceeding 29 bytes per call.
     *
     * Caller must issue write_enable() first.  Page boundary alignment is
     * the caller's responsibility (the eCos flash_program callback handles
     * this).
     */
    spi_load_addr_data(addr, buf, count);
    spi_xfer(W25Q_OP_PAGE_PROGRAM, (3U + count) * 8U, 0U);
    w25_flash_wait_ready();
}

void w25_flash_read_data(cyg_uint32 addr, cyg_uint8 *buf, cyg_uint32 count)
{
    /*
     * READ DATA (0x03): CMD=8, MOSI=24 (3-byte address), MISO=count×8.
     *
     * MoreBuf can handle at most 32 MISO bytes per transaction.  For
     * reads larger than 32 bytes this function loops in 29-byte chunks
     * (32 bytes FIFO - 3 address bytes in MOSI).
     *
     * NOTE: The eCos flash_read callback uses the faster CPU memory-mapped
     * SPI NOR window (KSEG1 0xBC000000) instead of calling this function.
     * This function is provided for completeness.
     */
    while (count > 0U) {
        cyg_uint32 chunk = count;
        if (chunk > 29U)
            chunk = 29U;

        spi_pack_addr(addr);    /* load 3-byte flash address into DATA0..DATA7 */
        spi_xfer(W25Q_OP_READ_DATA, 24U, chunk * 8U);
        spi_unpack_data(buf, chunk);

        buf   += chunk;
        addr  += chunk;
        count -= chunk;
    }
}

void w25_flash_chip_erase(void)
{
    /* CHIP ERASE (0xC7): CMD=8, no MOSI, no MISO.
     * Caller must issue write_enable() first. */
    spi_xfer(W25Q_OP_CHIP_ERASE, 0U, 0U);
    w25_flash_wait_ready();
}
