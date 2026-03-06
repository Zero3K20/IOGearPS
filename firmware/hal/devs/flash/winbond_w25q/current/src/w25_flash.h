/*
 * w25_flash.h — Winbond W25 series SPI NOR flash hardware API.
 *
 * Based on DexterHaslem/winbond-w25-flash-drv (MIT License).
 * Original: https://github.com/DexterHaslem/winbond-w25-flash-drv
 *
 * Adapted for eCos / MediaTek MT7688 (MIPS32, little-endian):
 *   - Uses eCos integer types (cyg_uint8, cyg_uint32) instead of <stdint.h>
 *   - Removes bitfield status-register structs (not needed by this driver)
 *   - The MT7688 implementation (w25_flash.c) uses the SoC's MoreBuf SPI
 *     controller rather than byte-by-byte SPI_EXCHANGE macros; both
 *     approaches expose the same API declared here.
 *
 * MIT License
 *
 * Copyright (c) DexterHaslem (original); IOGearPS project 2026 (MT7688 port)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef W25_FLASH_H
#define W25_FLASH_H

#include <cyg/infra/cyg_type.h>

/* ── Manufacturer / JEDEC constants ─────────────────────────────────────── */

#define WINBOND_MANU_ID     0xEFU   /* Winbond manufacturer ID               */

/* JEDEC ID for W25Q16JVSSIQ: manufacturer=0xEF, type=0x40, capacity=0x15 */
#define W25Q16_MEM_TYPE     0x40U   /* SPI NOR memory type                   */
#define W25Q16_CAPACITY     0x15U   /* 16 Mbit (2 MB)                        */

/* ── SPI NOR flash opcodes (W25Q series) ────────────────────────────────── */

#define W25Q_OP_WRITE_ENABLE        0x06U
#define W25Q_OP_WRITE_DISABLE       0x04U
#define W25Q_OP_READ_STATUS_REG_1   0x05U
#define W25Q_OP_READ_STATUS_REG_2   0x35U
#define W25Q_OP_READ_STATUS_REG_3   0x15U
#define W25Q_OP_READ_JEDEC_ID       0x9FU
#define W25Q_OP_READ_DATA           0x03U
#define W25Q_OP_FAST_READ_DATA      0x0BU
#define W25Q_OP_PAGE_PROGRAM        0x02U
#define W25Q_OP_SECTOR_ERASE        0x20U   /* 4 KB erase                    */
#define W25Q_OP_32KB_BLOCK_ERASE    0x52U
#define W25Q_OP_64KB_BLOCK_ERASE    0xD8U
#define W25Q_OP_CHIP_ERASE          0xC7U
#define W25Q_OP_ERASEPROG_SUSPEND   0x75U
#define W25Q_OP_ERASEPROG_RESUME    0x7AU
#define W25Q_OP_POWER_DOWN          0xB9U
#define W25Q_OP_DEVICE_ID_WAKEUP    0xABU
#define W25Q_OP_READ_UNIQUE_ID      0x4BU
#define W25Q_OP_ENABLE_RESET        0x66U
#define W25Q_OP_RESET_DEVICE        0x99U

/* Status Register 1 bits */
#define W25Q_SR1_BUSY   0x01U   /* Write / erase in progress                 */
#define W25Q_SR1_WEL    0x02U   /* Write enable latch                        */

/* ── JEDEC ID result structure ──────────────────────────────────────────── */

struct w25_jedec_id {
    cyg_uint8 manuf_id;     /* Manufacturer ID (0xEF for Winbond)            */
    cyg_uint8 mem_type;     /* Memory type     (0x40 for SPI NOR)            */
    cyg_uint8 capacity;     /* Capacity code   (0x15 = 16 Mbit / 2 MB)      */
};

/* ── W25Q flash operations ──────────────────────────────────────────────── */

/*
 * w25_flash_read_jedec()
 *   Read the 3-byte JEDEC ID into *jedec.
 */
void w25_flash_read_jedec(struct w25_jedec_id *jedec);

/*
 * w25_flash_read_status1()
 *   Return the value of Status Register 1 (contains the BUSY/WIP bit).
 */
cyg_uint8 w25_flash_read_status1(void);

/*
 * w25_flash_write_enable() / w25_flash_write_disable()
 *   Assert / deassert the Write Enable Latch (WEL).
 *   Must be called before every erase or page-program operation.
 */
void w25_flash_write_enable(void);
void w25_flash_write_disable(void);

/*
 * w25_flash_wait_ready()
 *   Spin-wait until the Write In Progress (WIP / BUSY) bit in Status
 *   Register 1 clears.  Called automatically by erase and program.
 */
void w25_flash_wait_ready(void);

/*
 * w25_flash_read_data()
 *   Read @count bytes from flash offset @addr into @buf using the SPI
 *   READ_DATA (0x03) command.
 *
 *   NOTE: The eCos flash_read callback uses the faster memory-mapped window
 *   instead of calling this function.  This function is provided for
 *   completeness and is available to application code.
 */
void w25_flash_read_data(cyg_uint32 addr, cyg_uint8 *buf, cyg_uint32 count);

/*
 * w25_flash_page_program()
 *   Program up to @count bytes from @buf into flash at offset @addr using
 *   the PAGE PROGRAM (0x02) command.
 *
 *   Constraints:
 *     - The caller MUST issue w25_flash_write_enable() immediately before.
 *     - @count must not exceed the remaining bytes in the current 256-byte
 *       page: count <= 256 - (addr & 0xFF).
 *     - The target flash range must have been erased beforehand.
 *
 *   MT7688 MoreBuf limit: at most 29 data bytes per SPI transaction
 *   (32-byte DATA FIFO minus 3 address bytes).  If @count > 29 the caller
 *   must split the write; the eCos flash_program callback does this
 *   automatically.
 */
void w25_flash_page_program(cyg_uint32 addr,
                             const cyg_uint8 *buf,
                             cyg_uint32 count);

/*
 * w25_flash_sector_erase()
 *   Erase the 4 KB sector that contains flash offset @addr using the
 *   SECTOR ERASE (0x20) command.
 *
 *   The caller MUST issue w25_flash_write_enable() immediately before.
 *   This function blocks until the erase completes.
 */
void w25_flash_sector_erase(cyg_uint32 addr);

/*
 * w25_flash_chip_erase()
 *   Erase the entire flash chip (CHIP ERASE 0xC7).
 *   The caller MUST issue w25_flash_write_enable() immediately before.
 */
void w25_flash_chip_erase(void);

#endif /* W25_FLASH_H */
