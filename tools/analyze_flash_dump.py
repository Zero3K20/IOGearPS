#!/usr/bin/env python3
"""
analyze_flash_dump.py — Analyze a full SPI NOR flash dump from an IOGear GPSU21.

A full flash dump contains every partition written to the 8 MB SPI NOR chip,
including the U-Boot bootloader and the application firmware.  This script
locates each partition, validates headers and checksums, and reports the
information that would be needed to recover a bricked device or to understand
why a custom firmware fails to flash.

Usage:
    python3 tools/analyze_flash_dump.py  <flash_dump.bin>
    python3 tools/analyze_flash_dump.py  <flash_dump.bin>  --extract-fw  fw_partition.bin

    flash_dump.bin    — raw binary read from the SPI NOR flash chip
                        (e.g.  flashrom -p ch341a_spi -r flash_dump.bin)
    --extract-fw      — also write the firmware partition bytes to a separate
                        file so they can be validated or reflashed via the web
                        interface

Creating the flash dump:

    # Using flashrom with a CH341A programmer:
    flashrom -p ch341a_spi -r gpsu21_flash_backup.bin

    # Note: read the chip manufacturer/part number first so flashrom can
    # select the correct chip parameters:
    flashrom -p ch341a_spi

What this script reports:
    - Flash chip size
    - U-Boot magic at offset 0x000000 (confirmed or absent)
    - U-Boot environment partition boundaries (if detectable)
    - Firmware (ZOT) partition start offset and size
    - ZOT header validation (magic, CRC, payload size)
    - U-Boot uImage header validation (magic, CRC, load address)
    - Firmware version string (must start with "J#MT7688-")
    - Packed version fields (major, minor, patch, build, suffix)
    - LZMA payload decompression (confirms the application image is intact)
    - Flash commands for U-Boot TFTP recovery (with actual partition addresses)

Why a full flash dump helps prevent bricking:

    The firmware .bin file produced by this repository
    (firmware/build/gpsu21_freertos.bin) is the APPLICATION PARTITION ONLY.
    It is NOT a full flash dump and must NOT be written at offset 0 — that
    would overwrite U-Boot and prevent the device from booting at all.

    A full flash dump from a working GPSU21 reveals:
      1. The exact start offset and size of the firmware partition, so the
         correct U-Boot erase/write addresses can be confirmed.
      2. The exact start offset and size of the U-Boot environment partition,
         where variables like fwaddr and firmware_addr are stored.
      3. The U-Boot bootloader binary itself, which can be disassembled to
         understand exactly what validation the upgrade path performs on a
         firmware image (version string format, magic bytes, CRC algorithm)
         before it is written to flash.
      4. A known-good image that can be written back to a bricked device to
         restore it to a working state without needing a donor unit.

    If you have a working GPSU21 and are willing to share a flash dump:
      - Run:  flashrom -p ch341a_spi -r gpsu21_flash_YYYYMMDD.bin
      - Run this script on the dump and share its output.
      - Optionally share the dump itself (it does not contain passwords or
        personal data — only firmware code and network configuration defaults).
"""

import sys
import os
import struct
import zlib
import lzma
import argparse


# ──────────────────────────────────────────────────────────────────────────────
# Known ZOT/uImage constants (confirmed from the OEM firmware binary)
# ──────────────────────────────────────────────────────────────────────────────

ZOT_MAGIC     = 0xb25a4758   # ZOT Technology firmware magic (LE uint32 at offset 4)
UIMAGE_MAGIC  = 0x27051956   # U-Boot uImage magic (BE uint32 at offset 0)
UBOOT_MAGIC   = 0x27051956   # U-Boot self (same magic at start of bootloader)
MIPS_LOAD_ADDR = 0x80500000  # Expected uImage load address (must match linker.ld)

ZOT_HEADER_SIZE   = 256  # bytes
UIMAGE_HEADER_SIZE = 64  # bytes

# Offset within the firmware (.bin) file where the LZMA payload starts.
# Must match package_firmware.py LZMA_OFFSET and unpack_gpsu21.py LZMA_OFFSET.
FIRMWARE_LZMA_OFFSET = 0x4AC0

# Typical SPI NOR flash sizes supported by the MT7688
KNOWN_FLASH_SIZES = [
    1 * 1024 * 1024,    #  1 MB
    2 * 1024 * 1024,    #  2 MB
    4 * 1024 * 1024,    #  4 MB
    8 * 1024 * 1024,    #  8 MB
    16 * 1024 * 1024,   # 16 MB
]


# ──────────────────────────────────────────────────────────────────────────────

def _crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def _fmt_addr(addr):
    return f"0x{addr:08X}"


def _locate_zot_partition(data):
    """
    Scan *data* for the ZOT firmware partition by looking for the ZOT magic
    (0xb25a4758) at a 4-byte aligned position.

    Returns a list of (offset, description) tuples for every match found.
    """
    matches = []
    offset = 0
    while offset + 8 <= len(data):
        magic = struct.unpack_from("<I", data, offset + 4)[0]
        if magic == ZOT_MAGIC:
            matches.append(offset)
        offset += 4
    return matches


def _validate_zot_header(data, fw_offset):
    """
    Validate the 256-byte ZOT header at *fw_offset* within *data*.
    Returns a dict of parsed/validated fields.
    """
    result = {}

    if fw_offset + ZOT_HEADER_SIZE > len(data):
        result["error"] = "ZOT header extends beyond dump"
        return result

    hdr = data[fw_offset:fw_offset + ZOT_HEADER_SIZE]

    stored_crc  = struct.unpack_from("<I", hdr, 0)[0]
    magic       = struct.unpack_from("<I", hdr, 4)[0]
    payload_sz  = struct.unpack_from("<I", hdr, 8)[0]

    # The ZOT header payload_size field counts bytes from 0x100 (uImage header)
    # to the end of the file, i.e. len(fw) - ZOT_HEADER_SIZE.
    # Total firmware size = ZOT_HEADER_SIZE + payload_sz.
    fw_end = fw_offset + ZOT_HEADER_SIZE + payload_sz
    if fw_end > len(data):
        result["warning"] = (
            f"Firmware end (0x{fw_end:X}) extends beyond dump (0x{len(data):X}); "
            "CRC cannot be validated (dump may be truncated)"
        )
        result["magic"]       = magic
        result["payload_size"] = payload_sz
        result["crc_ok"]      = None
    else:
        computed_crc = _crc32(data[fw_offset + 4 : fw_end]) ^ 0xFFFFFFFF
        result["magic"]        = magic
        result["payload_size"] = payload_sz
        result["stored_crc"]   = stored_crc
        result["computed_crc"] = computed_crc
        result["crc_ok"]       = (stored_crc == computed_crc)

    # Version string at offset 0x28 within the ZOT header
    ver_bytes = hdr[0x28:0x5B].rstrip(b"\x00")
    try:
        ver = ver_bytes.decode("latin-1")
    except Exception:
        ver = repr(ver_bytes)
    result["version_string"] = ver
    result["version_j_prefix"] = ver.startswith("J#MT7688-")

    # Packed version record at offsets 0x20–0x27
    result["packed_major"]  = hdr[0x20]
    result["packed_minor"]  = hdr[0x21]
    result["packed_patch"]  = hdr[0x22]
    result["packed_suffix"] = chr(hdr[0x23]) if 32 <= hdr[0x23] < 127 else f"0x{hdr[0x23]:02x}"
    result["packed_build"]  = struct.unpack_from("<I", hdr, 0x24)[0]

    return result


def _validate_uimage_header(data, fw_offset):
    """
    Validate the 64-byte U-Boot uImage header at fw_offset + 0x100.
    Returns a dict of parsed/validated fields.
    """
    result = {}

    uimage_offset = fw_offset + 0x100
    if uimage_offset + UIMAGE_HEADER_SIZE > len(data):
        result["error"] = "uImage header extends beyond dump"
        return result

    hdr = bytearray(data[uimage_offset:uimage_offset + UIMAGE_HEADER_SIZE])

    magic       = struct.unpack_from(">I", hdr,  0)[0]
    stored_hcrc = struct.unpack_from(">I", hdr,  4)[0]
    timestamp   = struct.unpack_from(">I", hdr,  8)[0]
    data_size   = struct.unpack_from(">I", hdr, 12)[0]
    load_addr   = struct.unpack_from(">I", hdr, 16)[0]
    entry_addr  = struct.unpack_from(">I", hdr, 20)[0]
    data_crc    = struct.unpack_from(">I", hdr, 24)[0]
    os_type     = hdr[28]
    arch        = hdr[29]
    img_type    = hdr[30]
    compression = hdr[31]
    name_bytes  = hdr[32:64].rstrip(b"\x00")
    try:
        name = name_bytes.decode("ascii")
    except Exception:
        name = repr(name_bytes)

    # Recalculate header CRC with CRC field zeroed
    struct.pack_into(">I", hdr, 4, 0)
    computed_hcrc = _crc32(bytes(hdr))

    result["magic"]       = magic
    result["magic_ok"]    = (magic == UIMAGE_MAGIC)
    result["stored_hcrc"] = stored_hcrc
    result["computed_hcrc"] = computed_hcrc
    result["hcrc_ok"]     = (stored_hcrc == computed_hcrc)
    result["timestamp"]   = timestamp
    result["data_size"]   = data_size
    result["load_addr"]   = load_addr
    result["load_addr_ok"] = (load_addr == MIPS_LOAD_ADDR)
    result["entry_addr"]  = entry_addr
    result["data_crc"]    = data_crc
    result["os_type"]     = os_type
    result["arch"]        = arch
    result["img_type"]    = img_type
    result["compression"] = compression
    result["name"]        = name

    return result


def _try_decompress_lzma(data, fw_offset):
    """
    Try to decompress the LZMA payload at fw_offset + FIRMWARE_LZMA_OFFSET.
    Returns (ok, size_or_error_msg).
    """
    lzma_offset = fw_offset + FIRMWARE_LZMA_OFFSET
    if lzma_offset >= len(data):
        return False, "LZMA payload offset exceeds dump size"
    try:
        raw = lzma.decompress(data[lzma_offset:])
        return True, len(raw)
    except Exception as e:
        return False, str(e)


def _check_mark(ok):
    if ok is True:
        return "✓"
    if ok is False:
        return "✗"
    return "?"


def analyze(dump_path, extract_fw_path=None):
    print(f"Loading flash dump: {dump_path}")
    with open(dump_path, "rb") as f:
        data = f.read()

    size = len(data)
    print(f"  Dump size: {size:,} bytes ({size / 1024:.1f} KiB / {size / 1048576:.2f} MiB)")

    # Check against known flash sizes
    closest = min(KNOWN_FLASH_SIZES, key=lambda s: abs(s - size))
    if size == closest:
        print(f"  Matches a standard SPI NOR flash size: {closest // 1048576} MiB ✓")
    elif abs(size - closest) / closest < 0.05:
        print(f"  Near a standard flash size ({closest // 1048576} MiB); "
              f"may be a partial read.")
    else:
        print(f"  Non-standard size — may be a partial read or wrong chip selected.")

    print()

    # ── Locate ZOT firmware partition ─────────────────────────────────────
    print("Scanning for ZOT firmware partition (magic 0xb25a4758)…")
    matches = _locate_zot_partition(data)

    if not matches:
        print("  ✗ ZOT magic not found in dump.")
        print()
        print("  Possible causes:")
        print("    - The dump is of the U-Boot partition only (not the full flash).")
        print("    - The firmware partition has been erased or overwritten.")
        print("    - The flash chip was read incorrectly (try a different programmer")
        print("      or verify the chip select signal).")
        print()
        # Try to detect U-Boot at offset 0
        if len(data) >= 4:
            first_word = struct.unpack_from(">I", data, 0)[0]
            if first_word == UBOOT_MAGIC:
                print("  U-Boot uImage magic found at offset 0x0 — this looks like")
                print("  the U-Boot partition, not the full flash dump.")
        return

    print(f"  Found {len(matches)} match(es):")
    for m in matches:
        print(f"    offset 0x{m:08X} ({m:,} bytes from start of dump)")

    fw_offset = matches[0]
    if len(matches) > 1:
        print(f"  Using first match at 0x{fw_offset:08X}.")
    print()

    # ── ZOT header ────────────────────────────────────────────────────────
    print(f"ZOT header (at 0x{fw_offset:08X}):")
    zot = _validate_zot_header(data, fw_offset)

    if "error" in zot:
        print(f"  ✗ {zot['error']}")
        return

    print(f"  Magic:         0x{zot['magic']:08x}  {_check_mark(zot['magic'] == ZOT_MAGIC)}")
    print(f"  Payload size:  {zot['payload_size']:,} bytes")

    if "crc_ok" in zot and zot["crc_ok"] is not None:
        ok = zot["crc_ok"]
        print(f"  CRC:           stored=0x{zot['stored_crc']:08x}  "
              f"computed=0x{zot['computed_crc']:08x}  {_check_mark(ok)}")
        if not ok:
            print("    ✗ CRC mismatch — firmware image may be corrupt.")
    elif "warning" in zot:
        print(f"  CRC:           {zot['warning']}")

    ver   = zot["version_string"]
    j_ok  = zot["version_j_prefix"]
    print(f"  Version:       {ver!r}  {_check_mark(j_ok)}")
    if not j_ok:
        print("    ✗ Version string does not start with 'J#MT7688-'.")
        print("      The ZOT upgrade validator requires this prefix.")
        print("      Firmware images without it will be rejected at flash time.")

    print(f"  Packed ver:    {zot['packed_major']}.{zot['packed_minor']:02d}."
          f"{zot['packed_patch']}.{zot['packed_build']}{zot['packed_suffix']}")

    print()

    # ── uImage header ─────────────────────────────────────────────────────
    print(f"uImage header (at 0x{fw_offset + 0x100:08X}):")
    uimg = _validate_uimage_header(data, fw_offset)

    if "error" in uimg:
        print(f"  ✗ {uimg['error']}")
        return

    print(f"  Magic:         0x{uimg['magic']:08x}  {_check_mark(uimg['magic_ok'])}")
    print(f"  Header CRC:    stored=0x{uimg['stored_hcrc']:08x}  "
          f"computed=0x{uimg['computed_hcrc']:08x}  {_check_mark(uimg['hcrc_ok'])}")
    print(f"  Load address:  0x{uimg['load_addr']:08x}  {_check_mark(uimg['load_addr_ok'])}")
    if not uimg["load_addr_ok"]:
        print(f"    ✗ Expected 0x{MIPS_LOAD_ADDR:08x} — the ZOT upgrade validator "
              "checks this field.")
    print(f"  Entry point:   0x{uimg['entry_addr']:08x}")
    print(f"  Data size:     {uimg['data_size']:,} bytes")
    print(f"  Image name:    {uimg['name']!r}")
    print(f"  OS/Arch/Type:  0x{uimg['os_type']:02x}/0x{uimg['arch']:02x}/0x{uimg['img_type']:02x}")
    print()

    # ── LZMA payload ──────────────────────────────────────────────────────
    print(f"LZMA payload (at 0x{fw_offset + FIRMWARE_LZMA_OFFSET:08X}):")
    ok, result = _try_decompress_lzma(data, fw_offset)
    if ok:
        print(f"  Decompressed:  {result:,} bytes  ✓")
    else:
        print(f"  ✗ Decompression failed: {result}")

    print()

    # ── Flash partition layout ────────────────────────────────────────────
    fw_total_size = ZOT_HEADER_SIZE + zot["payload_size"]
    fw_end        = fw_offset + fw_total_size
    uboot_size    = fw_offset  # U-Boot occupies everything before the firmware partition

    print("Flash partition layout (inferred from dump):")
    if uboot_size > 0:
        print(f"  0x{0:08X} – 0x{fw_offset - 1:08X}  ({uboot_size:,} bytes)  "
              f"U-Boot bootloader + environment")
    else:
        print(f"  (No data before firmware partition — this is a firmware-only file,")
        print(f"   not a full flash dump.  U-Boot partition not present.)")
    print(f"  0x{fw_offset:08X} – 0x{fw_end - 1:08X}  ({fw_total_size:,} bytes)  "
          f"Application firmware (ZOT + LZMA)")
    if fw_end < size:
        remaining = size - fw_end
        print(f"  0x{fw_end:08X} – 0x{size - 1:08X}  ({remaining:,} bytes)  "
              f"Remaining flash (free / overlay / other)")
    print()

    # ── U-Boot recovery commands ──────────────────────────────────────────
    print("U-Boot TFTP recovery commands:")
    print(f"  setenv ipaddr   192.168.0.1")
    print(f"  setenv serverip 192.168.0.100")
    print(f"  tftpboot 0x{MIPS_LOAD_ADDR:08x} gpsu21_freertos.bin")
    if uboot_size > 0:
        # Note: flash is at physical 0x1C000000 = KSEG1 0xBC000000 on MT7688
        flash_k1 = 0xBC000000 + fw_offset
        print(f"  # Firmware partition confirmed at flash offset 0x{fw_offset:08X}")
        print(f"  erase  0x{flash_k1:08X}  +{fw_total_size:#x}")
        print(f"  cp.b   0x{MIPS_LOAD_ADDR:08x}  0x{flash_k1:08X}  ${{filesize}}")
    else:
        print(f"  # Run 'printenv' in U-Boot to find the correct flash partition")
        print(f"  # address for your device (look for fwaddr or firmware_addr).")
        print(f"  # Example (addresses may differ on your device):")
        print(f"  erase  0xBC050000  +0x600000")
        print(f"  cp.b   0x{MIPS_LOAD_ADDR:08x}  0xBC050000  ${{filesize}}")
    print(f"  reset")
    print()
    print("  (Or use:  run upgradefirmware  if your U-Boot defines that variable)")
    print()

    # ── Extraction ────────────────────────────────────────────────────────
    if extract_fw_path:
        fw_bytes = data[fw_offset:fw_end]
        print(f"Extracting firmware partition → {extract_fw_path}")
        with open(extract_fw_path, "wb") as f:
            f.write(fw_bytes)
        print(f"  Wrote {len(fw_bytes):,} bytes")
        print()
        print("  Flash this file via the GPSU21 web interface (System → Upgrade),")
        print("  or via U-Boot TFTP as shown above.")
        print()

    # ── Summary ───────────────────────────────────────────────────────────
    checks = [
        ("ZOT magic",      zot["magic"] == ZOT_MAGIC),
        ("ZOT CRC",        zot.get("crc_ok")),
        ("J# version prefix", j_ok),
        ("uImage magic",   uimg["magic_ok"]),
        ("uImage hdr CRC", uimg["hcrc_ok"]),
        ("Load address",   uimg["load_addr_ok"]),
        ("LZMA payload",   ok),
    ]

    all_ok = all(v is True for _, v in checks)
    print("Summary:")
    for name, val in checks:
        print(f"  {_check_mark(val)}  {name}")
    print()
    if all_ok:
        print("All checks passed — firmware partition is valid and should flash successfully.")
    else:
        failed = [name for name, v in checks if v is not True]
        print(f"✗ {len(failed)} check(s) failed: {', '.join(failed)}")
        print()
        print("See individual check output above for details on what to fix.")
        print()
        print("Common causes of bricking when flashing a custom firmware:")
        print("  1. Version string at ZOT header offset 0x28 does not start with")
        print("     'J#MT7688-' — the ZOT upgrade validator rejects the image.")
        print("  2. ZOT CRC is wrong — the validator rejects the image before")
        print("     writing to flash.")
        print("  3. uImage load address is not 0x80500000 — the bootloader may")
        print("     decompress to the wrong RAM address and crash immediately.")
        print("  4. The hardware watchdog (WDT) was not disabled in the first")
        print("     few seconds of firmware startup — the SoC resets in a loop.")


# ──────────────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("dump", help="path to full SPI NOR flash dump (.bin)")
    parser.add_argument(
        "--extract-fw",
        metavar="OUTPUT",
        help="extract the firmware partition to OUTPUT",
    )
    args = parser.parse_args()

    if not os.path.isfile(args.dump):
        print(f"error: file not found: {args.dump!r}", file=sys.stderr)
        sys.exit(1)

    analyze(args.dump, extract_fw_path=args.extract_fw)
