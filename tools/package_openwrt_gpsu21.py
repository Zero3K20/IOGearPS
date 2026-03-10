#!/usr/bin/env python3
"""
package_openwrt_gpsu21.py — Wrap an OpenWrt kernel image for the IOGear GPSU21.

The GPSU21 uses a ZOT Technology proprietary bootloader that expects firmware
in a specific format (ZOT header + uImage header + stage-2 MIPS bootstrap +
LZMA-compressed payload).  See firmware/package_firmware.py for the full layout.

This script wraps an OpenWrt MT7688 kernel image — a raw vmlinux binary or an
OpenWrt uImage (initramfs-kernel.bin) — into that ZOT format, producing a .bin
that can be flashed via the GPSU21 web interface.


IMPORTANT: OpenWrt kernel load address
──────────────────────────────────────
The GPSU21 ZOT bootloader's stage-2 MIPS bootstrap always decompresses the
firmware payload to DRAM address 0x80500000 and jumps there.  An OpenWrt
kernel built for the standard MT7688 load address (0x80000000) will crash
immediately on boot because all absolute virtual-address references will be
wrong.

The kernel MUST be built with the physical start address set to 0x00500000
(virtual 0x80500000 in KSEG0):

    CONFIG_PHYSICAL_START=0x00500000

For OpenWrt's build system, also pass the matching load address to mkimage:

    KERNEL_LOADADDR=0x80500000

The config fragment in scripts/openwrt_gpsu21.config sets these automatically.
See the "OpenWrt alternative" section in README.md for full build instructions.


Usage
─────
    python3 tools/package_openwrt_gpsu21.py  <kernel>  <output.bin>  \\
            [--base-firmware OEM.bin|OEM.zip]  [--version VER]

    kernel           — OpenWrt kernel image: raw vmlinux binary (.bin) or an
                       OpenWrt uImage file (e.g. initramfs-kernel.bin).
                       For uImage input the 64-byte uImage header is stripped
                       automatically before LZMA re-packaging.

    output.bin       — path to write the flashable ZOT firmware image

    --base-firmware  — OEM firmware (.bin or .zip) to extract the stage-2
                       MIPS bootstrap from.
                       Default: MPS56_IOG_GPSU21_20171123.zip in the repo root
                       (the native IOGear GPSU21 firmware; LZMA@0x4AE0).

    --version        — ZOT header version string
                       (default: "H#MT7688-9.09.56.9032.00000394t-2017/11/23 10:36:02")


Example
───────
    # After building OpenWrt for ramips/mt76x8 with the GPSU21 config:
    python3 tools/package_openwrt_gpsu21.py \\
        openwrt-ramips-mt76x8-gpsu21-initramfs-kernel.bin \\
        gpsu21_openwrt.bin
"""

import sys
import os
import lzma
import struct
import zlib
import argparse
import time
import zipfile

# ──────────────────────────────────────────────────────────────────────────────
# Shared ZOT firmware layout constants (must match package_firmware.py).
# Duplicated here so this script works standalone without modifying sys.path.
# ──────────────────────────────────────────────────────────────────────────────

UIMAGE_MAGIC    = 0x27051956
UIMAGE_OFFSET   = 0x0100          # offset of 64-byte uImage header in the .bin
LZMA_OFFSET     = 0x4AE0          # offset of LZMA payload in the .bin (2017 IOGear fw)
BOOTSTRAP_SIZE  = LZMA_OFFSET - UIMAGE_OFFSET - 64   # 0x4AE0 - 0x0140 = 18848 B
MIPS_LOAD_ADDR  = 0x80500000
ZOT_MAGIC       = 0xb25a4758
ZOT_HEADER_SIZE = 256
VERSION_OFFSET      = 0x28
VERSION_PKT_OFFSET  = 0x20

DEFAULT_VERSION = "H#MT7688-9.09.56.9032.00000394t-2017/11/23 10:36:02"

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

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT  = os.path.dirname(_SCRIPT_DIR)
DEFAULT_BASE_FW = os.path.join(_REPO_ROOT, "MPS56_IOG_GPSU21_20171123.zip")


# ──────────────────────────────────────────────────────────────────────────────
# ZOT / uImage helpers (same logic as firmware/package_firmware.py)
# ──────────────────────────────────────────────────────────────────────────────

def _crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def _load_bootstrap(base_fw_path=None):
    """Extract the 18,848-byte stage-2 bootstrap from the OEM firmware."""
    if base_fw_path is None:
        base_fw_path = DEFAULT_BASE_FW
    base_fw_path = os.path.normpath(base_fw_path)
    if base_fw_path.lower().endswith(".zip"):
        with zipfile.ZipFile(base_fw_path) as zf:
            names = [n for n in zf.namelist() if n.lower().endswith(".bin")]
            if not names:
                raise ValueError(f"No .bin file found inside {base_fw_path}")
            fw = zf.read(names[0])
    else:
        with open(base_fw_path, "rb") as f:
            fw = f.read()

    if len(fw) < LZMA_OFFSET:
        raise ValueError(
            f"Base firmware {base_fw_path!r} is too small "
            f"({len(fw):,} bytes < {LZMA_OFFSET:#x})"
        )

    bootstrap = fw[UIMAGE_OFFSET + 64 : LZMA_OFFSET]
    if len(bootstrap) != BOOTSTRAP_SIZE:
        raise ValueError(
            f"Bootstrap region has unexpected size {len(bootstrap)} "
            f"(expected {BOOTSTRAP_SIZE})"
        )
    return bootstrap


def _parse_version_fields(version_str):
    """Parse packed version fields from the version string."""
    import re
    m = re.match(
        r"(?:[A-Z]#)?MT7688-(\d+)\.(\d+)\.(\d+)\.\d+\.(\d+)([a-zA-Z]?)",
        version_str)
    if not m:
        return None
    return (int(m.group(1)), int(m.group(2)), int(m.group(3)),
            ord(m.group(5)) if m.group(5) else 0, int(m.group(4)))


def _build_uimage_header(data_crc, data_size, timestamp):
    """Build a 64-byte U-Boot uImage header."""
    hdr = bytearray(64)
    struct.pack_into(">I", hdr,  0, UIMAGE_MAGIC)
    struct.pack_into(">I", hdr,  4, 0)
    struct.pack_into(">I", hdr,  8, timestamp)
    struct.pack_into(">I", hdr, 12, data_size)
    struct.pack_into(">I", hdr, 16, MIPS_LOAD_ADDR)
    struct.pack_into(">I", hdr, 20, MIPS_LOAD_ADDR)
    struct.pack_into(">I", hdr, 24, data_crc)
    hdr[28] = 0x05
    hdr[29] = 0x05
    hdr[30] = 0x01
    hdr[31] = 0x00
    name = b"zot716u2"
    hdr[32:32 + len(name)] = name
    hcrc = _crc32(bytes(hdr))
    struct.pack_into(">I", hdr, 4, hcrc)
    return bytes(hdr)


def _build_zot_header_stub(payload_size, version_str):
    """Build the 256-byte ZOT firmware header (without the final checksum)."""
    hdr = bytearray(ZOT_HEADER_SIZE)
    struct.pack_into("<I", hdr, 4, ZOT_MAGIC)
    struct.pack_into("<I", hdr, 8, payload_size)
    fields = _parse_version_fields(version_str)
    if fields:
        major, minor, patch, suffix_byte, build_count = fields
        hdr[VERSION_PKT_OFFSET]     = major  & 0xFF
        hdr[VERSION_PKT_OFFSET + 1] = minor  & 0xFF
        hdr[VERSION_PKT_OFFSET + 2] = patch  & 0xFF
        hdr[VERSION_PKT_OFFSET + 3] = suffix_byte & 0xFF
        struct.pack_into("<I", hdr, VERSION_PKT_OFFSET + 4, build_count)
    ver_bytes = version_str[:51].encode("latin-1")
    hdr[VERSION_OFFSET : VERSION_OFFSET + len(ver_bytes)] = ver_bytes
    struct.pack_into("<I", hdr, 0, 0)
    return bytes(hdr)


def _patch_zot_crc(fw_bytes):
    """Compute and write the ZOT header checksum into fw_bytes[0:4]."""
    crc = _crc32(bytes(fw_bytes[4:])) ^ 0xFFFFFFFF
    struct.pack_into("<I", fw_bytes, 0, crc)


# ──────────────────────────────────────────────────────────────────────────────
# OpenWrt-specific input handling
# ──────────────────────────────────────────────────────────────────────────────

def _strip_uimage_header(data):
    """
    If *data* begins with a U-Boot uImage magic number, strip the 64-byte
    header and return the raw payload.  Otherwise return *data* unchanged.

    OpenWrt delivers its initramfs kernel as a uImage (initramfs-kernel.bin).
    The payload inside is typically LZMA-compressed vmlinux + initramfs.
    """
    if len(data) >= 4 and struct.unpack_from(">I", data, 0)[0] == UIMAGE_MAGIC:
        print("  Detected uImage header — stripping 64-byte header.")
        return data[64:]
    return data


def _is_lzma_alone(data):
    """
    Return True if *data* looks like an LZMA alone-format stream.

    The LZMA alone format begins with:
      - 1 byte properties (lc/lp/pb encoded, valid range 0x00–0xE4)
      - 4 bytes little-endian dict size
      - 8 bytes little-endian uncompressed size (0xFFFFFFFFFFFFFFFF = unknown)
    """
    if len(data) < 13:
        return False
    props = data[0]
    if props > 0xE4:
        return False
    # Quick sanity-check: try decompressing a small prefix
    try:
        dec = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE)
        dec.decompress(data[:min(len(data), 4096)])
        return True
    except lzma.LZMAError:
        return False


# ──────────────────────────────────────────────────────────────────────────────
# Main packaging function
# ──────────────────────────────────────────────────────────────────────────────

def package_openwrt(kernel_path, output_path, version_str=DEFAULT_VERSION,
                    base_fw_path=None):
    """
    Wrap an OpenWrt MT7688 kernel image in the GPSU21 ZOT firmware format.

    The kernel must have been compiled with load/entry address 0x80500000:
        CONFIG_PHYSICAL_START=0x00500000
    """
    print(f"Loading OpenWrt kernel: {kernel_path}")
    with open(kernel_path, "rb") as f:
        kernel_data = f.read()
    print(f"  Input size:  {len(kernel_data):,} bytes")

    # Strip outer uImage header produced by OpenWrt's mkimage invocation
    payload = _strip_uimage_header(kernel_data)
    if len(payload) < len(kernel_data):
        print(f"  Payload:     {len(payload):,} bytes")

    # Determine whether payload is already LZMA-compressed (typical for OpenWrt
    # uImage contents) or a raw binary (vmlinux.bin from a custom build).
    if _is_lzma_alone(payload):
        print("  Payload format: LZMA alone — decompressing to re-compress with ZOT settings …")
        try:
            raw = lzma.decompress(payload, format=lzma.FORMAT_ALONE)
        except lzma.LZMAError as exc:
            sys.exit(f"ERROR: Failed to decompress LZMA payload: {exc}\n"
                     "Ensure the input file is a valid OpenWrt initramfs-kernel.bin "
                     "or a raw vmlinux binary.")
        print(f"  Decompressed: {len(raw):,} bytes")
        print("  Re-compressing with ZOT LZMA parameters …")
        lzma_payload = lzma.compress(raw, format=lzma.FORMAT_ALONE,
                                     filters=LZMA_FILTERS)
    else:
        raw = payload
        print(f"  Payload format: raw binary — compressing with LZMA …")
        lzma_payload = lzma.compress(raw, format=lzma.FORMAT_ALONE,
                                     filters=LZMA_FILTERS)

    print(f"  Compressed:  {len(lzma_payload):,} bytes")

    # Load the stage-2 MIPS bootstrap from the OEM firmware
    base_label = os.path.basename(base_fw_path or DEFAULT_BASE_FW)
    print(f"Loading stage-2 bootstrap from: {base_label}")
    bootstrap = _load_bootstrap(base_fw_path)
    print(f"  Bootstrap:   {len(bootstrap):,} bytes (0x{UIMAGE_OFFSET+64:04X}–0x{LZMA_OFFSET-1:04X})")

    # Assemble the complete firmware image
    uimage_data  = bootstrap + lzma_payload
    data_crc     = _crc32(uimage_data)
    timestamp    = int(time.time())

    uimage_hdr   = _build_uimage_header(data_crc, len(uimage_data), timestamp)
    payload_size = len(uimage_hdr) + len(bootstrap) + len(lzma_payload)
    zot_hdr      = _build_zot_header_stub(payload_size, version_str)

    fw = bytearray(zot_hdr + uimage_hdr + bootstrap + lzma_payload)
    _patch_zot_crc(fw)
    fw = bytes(fw)

    assert len(zot_hdr) == ZOT_HEADER_SIZE
    assert len(uimage_hdr) == 64
    assert len(bootstrap) == BOOTSTRAP_SIZE

    with open(output_path, "wb") as f:
        f.write(fw)

    print(f"\nWrote {len(fw):,} bytes → {output_path}")
    print(f"  Version string: {version_str}")
    print(f"  LZMA offset:    0x{LZMA_OFFSET:04X}")
    print()
    print("Flash via the GPSU21 web interface:")
    print("  1. Open http://<printer-ip>/ in a browser")
    print("  2. Navigate to System → Upgrade")
    print(f"  3. Upload {os.path.basename(output_path)}")
    print("  4. Do NOT power-cycle during the upgrade (~60 s)")
    print()
    print("REMINDER: the OpenWrt kernel must be compiled with")
    print("  CONFIG_PHYSICAL_START=0x00500000 (load address 0x80500000).")
    print("See scripts/build_openwrt_gpsu21.sh for full build instructions.")


# ──────────────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("kernel",
                        help="OpenWrt kernel image: raw vmlinux binary or "
                             "OpenWrt uImage (initramfs-kernel.bin)")
    parser.add_argument("output",
                        help="output flashable ZOT firmware image (.bin)")
    parser.add_argument("--version", default=DEFAULT_VERSION,
                        help="ZOT header version string (default: %(default)s)")
    parser.add_argument("--base-firmware", default=None, dest="base_fw",
                        metavar="OEM_FW",
                        help="OEM .bin or .zip to extract stage-2 bootstrap from "
                             "(default: MPS56_IOG_GPSU21_20171123.zip in repo root)")
    args = parser.parse_args()

    package_openwrt(args.kernel, args.output, args.version, args.base_fw)
