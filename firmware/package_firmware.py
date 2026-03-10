#!/usr/bin/env python3
"""
package_firmware.py — Package a raw FreeRTOS binary into an IOGear GPSU21
                       flashable firmware image.

The GPSU21 bootloader expects a specific binary layout:

    Offset    Size    Content
    ──────────────────────────────────────────────────────────
    0x0000    256 B   ZOT Technology firmware header
                       0x00–0x03  CRC32 (bitwise complement, covers bytes 4–end)
                       0x04–0x07  Magic: 0xb25a4758 (ZXT)
                       0x08–0x0B  Payload size (LE uint32, counts bytes 0x0100–end)
                       0x0C–0x1F  (all zeros in both verified OEM firmware images)
                       0x20        Version major (integer byte, e.g. 9)
                       0x21        Version minor (integer byte, e.g. 9)
                       0x22        Version patch (integer byte, e.g. 56)
                       0x23        Version suffix char (ASCII, e.g. 't' = 0x74)
                       0x24–0x27  Build count (LE uint32, e.g. 1243)
                       0x28–0x5B  Version string (null-terminated, 51 chars max).
                                   First 2 bytes encode the OEM product code as a
                                   LE uint16 rendered in ASCII: e.g. 9034 (0x234A)
                                   → bytes 0x4A 0x23 = "J#" (2019 OEM firmware);
                                   9032 (0x2348) → 0x48 0x23 = "H#" (2017 IOGear).
    0x0100     64 B   U-Boot uImage header (ZOT-specific fields, big-endian)
                       0x00–0x03  Magic: 0x27051956
                       0x04–0x07  Header CRC32 (covers header with this field zeroed)
                       0x08–0x0B  Timestamp (Unix epoch, big-endian)
                       0x0C–0x0F  Data size (big-endian) = len(padding + lzma_payload)
                       0x10–0x13  Load address: 0x80500000 (big-endian)
                       0x14–0x17  Entry point: 0x80500000 (big-endian)
                       0x18–0x1B  Data CRC32 (big-endian)
                       0x1C        OS type: 0x05 (matches OEM firmware)
                       0x1D        Architecture: 0x05 (matches OEM firmware)
                       0x1E        Image type: 0x01 (matches OEM firmware)
                       0x1F        Compression: 0x00 (matches OEM firmware)
                       0x20–0x3F  Image name: "zot716u2" (null-padded)
    0x0140   18816 B  Padding (0xFF bytes) — gap between uImage header and LZMA
    0x4AC0    var     LZMA-compressed FreeRTOS binary

Usage:
    python3 firmware/package_firmware.py  <input.bin>  <output.bin>  [--version VER]

    input.bin   — raw FreeRTOS binary (output of objcopy -O binary)
    output.bin  — output file for the flashable firmware image
    --version   — override the version string embedded in the ZOT header
                  (default: "J#MT7688-9.09.56.9034.00001243t-2019/11/19 13:00:10")

    The two-character prefix (e.g. "J#" or "H#") in the version string encodes
    the OEM product code as a little-endian uint16 in ASCII.  The 2019 OEM
    reference firmware uses "J#" (product code 9034) and the original 2017
    IOGear-branded firmware uses "H#" (product code 9032).

Example:
    python3 firmware/package_firmware.py  build/gpsu21_app.bin  build/gpsu21_freertos.bin
"""

import sys
import os
import lzma
import struct
import zlib
import argparse
import time


# ──────────────────────────────────────────────────────────────────────────────
# Layout constants (must match the values the GPSU21 bootloader expects)
# ──────────────────────────────────────────────────────────────────────────────

UIMAGE_OFFSET  = 0x0100   # offset of the 64-byte uImage header in the .bin
LZMA_OFFSET    = 0x4AC0   # offset of the LZMA payload in the .bin
PADDING_SIZE   = LZMA_OFFSET - UIMAGE_OFFSET - 64   # 0x4AC0 - 0x0140 = 18816 B

# Load/entry address: the ZOT/U-Boot bootloader decompresses the LZMA payload
# to this KSEG0 DRAM address and jumps there.  This MUST match the address the
# firmware binary was linked for (linker.ld ORIGIN) and must match the value
# in the OEM firmware's uImage header; the upgrade validator checks this field.
MIPS_LOAD_ADDR = 0x80500000
UIMAGE_MAGIC   = 0x27051956

ZOT_MAGIC      = 0xb25a4758   # ZOT Technology magic (verified from OEM firmware)
ZOT_HEADER_SIZE = 256          # bytes
VERSION_OFFSET  = 0x28         # within ZOT header
VERSION_PKT_OFFSET = 0x20      # packed version record within ZOT header

DEFAULT_VERSION = "J#MT7688-9.09.56.9034.00001243t-2019/11/19 13:00:10"

# LZMA filter parameters that match the original firmware
LZMA_FILTERS = [
    {
        "id":        lzma.FILTER_LZMA1,
        "dict_size": 8 * 1024 * 1024,
        "lc":        3,
        "lp":        0,
        "pb":        2,
        "preset":    lzma.PRESET_DEFAULT,
    }
]


def _crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def _build_uimage_header(data_crc, data_size, timestamp):
    """
    Build a 64-byte U-Boot uImage header.

    data_crc   — CRC32 of (padding || lzma_payload)
    data_size  — byte length of (padding || lzma_payload)
    timestamp  — Unix timestamp to embed

    The OS/arch/image-type/compression fields are set to match the OEM firmware
    values exactly (0x05/0x05/0x01/0x00).  These values do not follow standard
    U-Boot semantics; the ZOT bootloader uses them as-is.  Changing them causes
    the upgrade validator to reject the image with "Wrong firmware".
    """
    hdr = bytearray(64)

    struct.pack_into(">I", hdr,  0, UIMAGE_MAGIC)    # magic
    struct.pack_into(">I", hdr,  4, 0)                # header CRC (filled below)
    struct.pack_into(">I", hdr,  8, timestamp)        # timestamp
    struct.pack_into(">I", hdr, 12, data_size)        # data size
    struct.pack_into(">I", hdr, 16, MIPS_LOAD_ADDR)  # load address
    struct.pack_into(">I", hdr, 20, MIPS_LOAD_ADDR)  # entry point
    struct.pack_into(">I", hdr, 24, data_crc)         # data CRC

    hdr[28] = 0x05   # OS type:    matches OEM firmware
    hdr[29] = 0x05   # Arch:       matches OEM firmware
    hdr[30] = 0x01   # Image type: matches OEM firmware
    hdr[31] = 0x00   # Compression: matches OEM firmware (bootloader handles LZMA)

    # Image name: "zot716u2" (null-padded to 32 bytes)
    name = b"zot716u2"
    hdr[32:32 + len(name)] = name

    # Recalculate header CRC with the CRC field zeroed
    hcrc = _crc32(bytes(hdr))
    struct.pack_into(">I", hdr, 4, hcrc)

    return bytes(hdr)


def _parse_version_fields(version_str):
    """
    Parse the packed version fields from the version string for ZOT header
    offsets 0x20–0x27.

    The OEM firmware encodes a packed version record in the ZOT header at these
    offsets.  Reverse-engineering of the OEM binary confirms the layout:
      0x20  — version major  (integer byte, e.g. 9)
      0x21  — version minor  (integer byte, e.g. 9 from "09")
      0x22  — version patch  (integer byte, e.g. 56)
      0x23  — build suffix   (ASCII char,   e.g. ord('t') = 0x74)
      0x24–0x27 — build count (LE uint32,  e.g. 1243)

    The upgrade validator in the running firmware checks these bytes; if they
    are zero the firmware is rejected as "Wrong firmware".

    Expected version_str format: "{OEM}#MT7688-{maj}.{min}.{patch}.{ignored}.{count}{suffix}-..."
    Example (2019 OEM): "J#MT7688-9.09.56.9034.00001243t-2019/11/19 13:00:10"
    Example (2017 IOG): "H#MT7688-9.09.56.9032.00000394t-2017/11/23 10:36:02"
    Returns (major, minor, patch, suffix_char, build_count) or None on parse failure.
    """
    import re
    # Strip optional leading 2-byte OEM prefix (e.g. "J#" = product 9034,
    # "H#" = product 9032) and platform prefix "MT7688-".
    # The prefix is optional to allow bare "MT7688-..." strings for
    # compatibility, though all verified OEM firmwares include it.
    # int() handles leading zeros in version components (e.g. "09" → 9)
    m = re.match(
        r"(?:[A-Z]#)?MT7688-(\d+)\.(\d+)\.(\d+)\.\d+\.(\d+)([a-zA-Z]?)",
        version_str)
    if not m:
        return None
    major      = int(m.group(1))
    minor      = int(m.group(2))
    patch      = int(m.group(3))
    build      = int(m.group(4))
    suffix     = ord(m.group(5)) if m.group(5) else 0
    return (major, minor, patch, suffix, build)


def _build_zot_header_stub(payload_size, version_str):
    """
    Build the 256-byte ZOT Technology firmware header WITHOUT the checksum.

    payload_size — number of bytes after this header (i.e. len(fw) - 256)
    version_str  — version string to embed at offset 0x28

    The checksum at offset 0x00–0x03 is a bitwise complement of CRC32 over
    fw[4:] — i.e. it covers the entire firmware image after the first 4 bytes.
    It must be computed AFTER the complete image is assembled; call
    _patch_zot_crc() on the assembled bytearray to fill it in.

    Bytes 0x20–0x27 hold a packed version record that the upgrade validator
    cross-checks; they are populated from the version string.
    """
    hdr = bytearray(ZOT_HEADER_SIZE)

    # Magic at offset 0x04
    struct.pack_into("<I", hdr, 4, ZOT_MAGIC)

    # Payload size at offset 0x08 (LE uint32, counts bytes from 0x100 to end)
    struct.pack_into("<I", hdr, 8, payload_size)

    # Packed version record at offsets 0x20–0x27 (must match OEM firmware layout)
    fields = _parse_version_fields(version_str)
    if fields:
        major, minor, patch, suffix_byte, build_count = fields
        hdr[VERSION_PKT_OFFSET]     = major  & 0xFF
        hdr[VERSION_PKT_OFFSET + 1] = minor  & 0xFF
        hdr[VERSION_PKT_OFFSET + 2] = patch  & 0xFF
        hdr[VERSION_PKT_OFFSET + 3] = suffix_byte & 0xFF
        struct.pack_into("<I", hdr, VERSION_PKT_OFFSET + 4, build_count)

    # Version string at offset 0x28 (null-terminated, up to 51 chars + null).
    # The OEM firmware stores a 51-char string at 0x28–0x5A with null at 0x5B.
    # Slice the string (not the bytes) so the [:51] limit is in characters;
    # latin-1 is a 1-byte encoding so each char encodes to exactly 1 byte.
    ver_bytes = version_str[:51].encode("latin-1")
    hdr[VERSION_OFFSET : VERSION_OFFSET + len(ver_bytes)] = ver_bytes

    # CRC placeholder — must be patched after the full image is assembled
    struct.pack_into("<I", hdr, 0, 0)

    return bytes(hdr)


def _patch_zot_crc(fw_bytes):
    """
    Compute and write the ZOT header checksum into fw_bytes[0:4].

    The checksum is the bitwise complement of CRC32(fw_bytes[4:]).
    fw_bytes must be a bytearray.
    """
    crc = _crc32(bytes(fw_bytes[4:])) ^ 0xFFFFFFFF
    struct.pack_into("<I", fw_bytes, 0, crc)


def package(input_path, output_path, version_str=DEFAULT_VERSION):
    print(f"Loading raw FreeRTOS binary: {input_path}")
    with open(input_path, "rb") as f:
        raw = f.read()
    print(f"  Input size:       {len(raw):,} bytes")

    # Compress the firmware binary with LZMA
    print("Compressing with LZMA …")
    lzma_payload = lzma.compress(raw, format=lzma.FORMAT_ALONE,
                                  filters=LZMA_FILTERS)
    print(f"  Compressed size:  {len(lzma_payload):,} bytes")

    # Build the padding region (0x0140–0x4ABF, all 0xFF)
    padding = b"\xFF" * PADDING_SIZE

    # The uImage "data" field covers padding + lzma_payload
    uimage_data = padding + lzma_payload
    data_crc    = _crc32(uimage_data)
    timestamp   = int(time.time())

    # Build headers
    uimage_hdr = _build_uimage_header(data_crc, len(uimage_data), timestamp)

    # ZOT payload_size = len(uimage_hdr) + len(padding) + len(lzma_payload)
    payload_size = len(uimage_hdr) + len(padding) + len(lzma_payload)
    zot_hdr = _build_zot_header_stub(payload_size, version_str)

    # Assemble the complete firmware image
    fw = bytearray(zot_hdr + uimage_hdr + padding + lzma_payload)

    # Patch the ZOT checksum now that the full image is assembled
    _patch_zot_crc(fw)

    fw = bytes(fw)

    # Verify layout
    assert len(zot_hdr) == ZOT_HEADER_SIZE
    assert len(uimage_hdr) == 64
    assert len(padding) == PADDING_SIZE

    with open(output_path, "wb") as f:
        f.write(fw)

    print(f"\nWrote {len(fw):,} bytes → {output_path}")
    print(f"  Version string:   {version_str}")
    print(f"  LZMA offset:      0x{LZMA_OFFSET:04X}")
    print()
    print("Flash via the GPSU21 web interface:")
    print("  1. Open http://<printer-ip>/ in a browser")
    print("  2. Navigate to System → Upgrade")
    print(f"  3. Upload {os.path.basename(output_path)}")
    print("  4. Do NOT power-cycle during the upgrade (~60 s)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input",   help="raw FreeRTOS binary (.bin)")
    parser.add_argument("output",  help="output firmware image (.bin)")
    parser.add_argument("--version", default=DEFAULT_VERSION,
                        help="firmware version string (default: %(default)s)")
    args = parser.parse_args()

    package(args.input, args.output, args.version)
