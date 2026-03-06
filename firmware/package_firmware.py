#!/usr/bin/env python3
"""
package_firmware.py — Package a raw FreeRTOS binary into an IOGear GPSU21
                       flashable firmware image.

The GPSU21 bootloader expects a specific binary layout:

    Offset    Size    Content
    ──────────────────────────────────────────────────────────
    0x0000    256 B   ZOT Technology firmware header
                       0x00–0x03  CRC32 (bitwise complement, covers bytes 4–end)
                       0x04–0x07  Magic: 0x68617964 ("hayd")
                       0x08–0x0B  Payload size (LE uint32, counts bytes 0x0100–end)
                       0x28–0x5A  Version string (null-terminated)
    0x0100     64 B   U-Boot uImage header (MIPS standalone, big-endian fields)
                       0x00–0x03  Magic: 0x27051956
                       0x04–0x07  Header CRC32 (covers header with this field zeroed)
                       0x08–0x0B  Timestamp (Unix epoch, big-endian)
                       0x0C–0x0F  Data size (big-endian) = len(padding + lzma_payload)
                       0x10–0x13  Load address: 0x80000400 (big-endian)
                       0x14–0x17  Entry point: 0x80000400 (big-endian)
                       0x18–0x1B  Data CRC32 (big-endian)
                       0x1C        OS type: 0x00 (invalid / standalone)
                       0x1D        Architecture: 0x08 (MIPS)
                       0x1E        Image type: 0x05 (standalone)
                       0x1F        Compression: 0x03 (LZMA)
                       0x20–0x3F  Image name: "zot716u2" (null-padded)
    0x0140   19072 B  Padding (0xFF bytes) — gap between uImage header and LZMA
    0x4AC0    var     LZMA-compressed eCos binary

Usage:
    python3 firmware/package_firmware.py  <input.bin>  <output.bin>  [--version VER]

    input.bin   — raw eCos binary (output of objcopy -O binary)
    output.bin  — output file for the flashable firmware image
    --version   — override the version string embedded in the ZOT header
                  (default: "MT7688-9.09.56.9034.00001243t-2019/11/19 13:00:10")

Example:
    python3 firmware/package_firmware.py  build/gpsu21_app.bin  build/gpsu21_ecos.bin
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
PADDING_SIZE   = LZMA_OFFSET - UIMAGE_OFFSET - 64   # 0x4AC0 - 0x0140 = 19072 B

MIPS_LOAD_ADDR = 0x80000400
UIMAGE_MAGIC   = 0x27051956

ZOT_MAGIC      = 0x68617964   # "hayd" LE
ZOT_HEADER_SIZE = 256          # bytes
VERSION_OFFSET  = 0x28         # within ZOT header

DEFAULT_VERSION = "MT7688-9.09.56.9034.00001243t-2019/11/19 13:00:10"

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
    """
    hdr = bytearray(64)

    struct.pack_into(">I", hdr,  0, UIMAGE_MAGIC)    # magic
    struct.pack_into(">I", hdr,  4, 0)                # header CRC (filled below)
    struct.pack_into(">I", hdr,  8, timestamp)        # timestamp
    struct.pack_into(">I", hdr, 12, data_size)        # data size
    struct.pack_into(">I", hdr, 16, MIPS_LOAD_ADDR)  # load address
    struct.pack_into(">I", hdr, 20, MIPS_LOAD_ADDR)  # entry point
    struct.pack_into(">I", hdr, 24, data_crc)         # data CRC

    hdr[28] = 0x00   # OS type: invalid/standalone
    hdr[29] = 0x08   # Architecture: MIPS
    hdr[30] = 0x05   # Image type: standalone
    hdr[31] = 0x03   # Compression: LZMA

    # Image name: "zot716u2" (null-padded to 32 bytes)
    name = b"zot716u2"
    hdr[32:32 + len(name)] = name

    # Recalculate header CRC with the CRC field zeroed
    hcrc = _crc32(bytes(hdr))
    struct.pack_into(">I", hdr, 4, hcrc)

    return bytes(hdr)


def _build_zot_header_stub(payload_size, version_str):
    """
    Build the 256-byte ZOT Technology firmware header WITHOUT the checksum.

    payload_size — number of bytes after this header (i.e. len(fw) - 256)
    version_str  — version string to embed at offset 0x28

    The checksum at offset 0x00–0x03 is a bitwise complement of CRC32 over
    fw[4:] — i.e. it covers the entire firmware image after the first 4 bytes.
    It must be computed AFTER the complete image is assembled; call
    _patch_zot_crc() on the assembled bytearray to fill it in.
    """
    hdr = bytearray(ZOT_HEADER_SIZE)

    # Magic at offset 0x04
    struct.pack_into("<I", hdr, 4, ZOT_MAGIC)

    # Payload size at offset 0x08 (LE uint32, counts bytes from 0x100 to end)
    struct.pack_into("<I", hdr, 8, payload_size)

    # Version string at offset 0x28 (null-terminated)
    ver_bytes = version_str.encode("latin-1")[:50]
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

    # Compress the eCos binary with LZMA
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
    parser.add_argument("input",   help="raw eCos binary (.bin)")
    parser.add_argument("output",  help="output firmware image (.bin)")
    parser.add_argument("--version", default=DEFAULT_VERSION,
                        help="firmware version string (default: %(default)s)")
    args = parser.parse_args()

    package(args.input, args.output, args.version)
