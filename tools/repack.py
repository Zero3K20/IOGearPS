#!/usr/bin/env python3
"""
repack.py — Rebuild a PS-1206U firmware image from unpacked components.

Usage:
    python3 tools/repack.py [unpacked_dir] [output.bin]

    unpacked_dir  — directory produced by tools/unpack.py
                    (default: firmware_unpacked)
    output.bin    — path for the repacked firmware
                    (default: PS-1206U_repacked.bin)

The script reads the following files from unpacked_dir:
    header1.bin      — 32-byte Edimax8820 header
    PS06EPS.BIN      — main print-server firmware
    header2.bin      — 32-byte Edimax7420 header
    PS06UPG.BIN      — upgrade utility
    bootloader.bin   — ROM bootloader (0x8000 bytes, occupies end of flash)

It recompresses each .BIN into a ZIP archive (DEFLATE, same as the originals)
and assembles them in the correct flash layout.  The resulting image can be
flashed via the web interface's System → Upgrade page.

WARNING
-------
The x86 code inside PS06EPS.BIN and PS06UPG.BIN is real-mode 16-bit machine
code compiled by the original Edimax/IOGear toolchain.  There is no open-source
compiler or linker that can regenerate these binaries from scratch.  The typical
workflow is:

  1. Unpack  →  tools/unpack.py
  2. Disassemble  →  use Ghidra (x86:LE:16:default) or IDA Pro / Binary Ninja
  3. Binary-patch PS06EPS.BIN with a hex editor (or a custom Python script) to
     change a specific instruction, string, or data value
  4. Repack  →  tools/repack.py  (this script)
  5. Flash  →  web interface System → Upgrade

The HTML/CSS/JS web-UI pages embedded in PS06EPS.BIN can be located and
modified without a disassembler; see README.md for details.
"""

import argparse
import io
import os
import sys
import zipfile

FIRMWARE_SIZE     = 0x80000   # 512 KB
HEADER1_OFFSET    = 0x00000
HEADER1_SIZE      = 32
ZIP1_OFFSET       = HEADER1_OFFSET + HEADER1_SIZE   # 0x00020
HEADER2_OFFSET    = 0x60000
HEADER2_SIZE      = 32
ZIP2_OFFSET       = HEADER2_OFFSET + HEADER2_SIZE   # 0x60020
BOOTLOADER_OFFSET = 0x78000
BOOTLOADER_SIZE   = FIRMWARE_SIZE - BOOTLOADER_OFFSET  # 0x8000


def make_zip(filename: str, content: bytes) -> bytes:
    """Return a ZIP archive containing *content* stored as *filename*."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        zf.writestr(filename, content)
    return buf.getvalue()


def read(directory: str, filename: str) -> bytes:
    path = os.path.join(directory, filename)
    with open(path, "rb") as fh:
        return fh.read()


def main():
    parser = argparse.ArgumentParser(description="Repack a PS-1206U firmware .bin file")
    parser.add_argument("unpacked", nargs="?", default="firmware_unpacked",
                        help="Directory produced by unpack.py (default: firmware_unpacked)")
    parser.add_argument("output", nargs="?", default="PS-1206U_repacked.bin",
                        help="Output .bin filename (default: PS-1206U_repacked.bin)")
    args = parser.parse_args()

    # ---- Read components ----
    print(f"Reading components from '{args.unpacked}/'...")
    header1    = read(args.unpacked, "header1.bin")
    eps_bin    = read(args.unpacked, "PS06EPS.BIN")
    header2    = read(args.unpacked, "header2.bin")
    upg_bin    = read(args.unpacked, "PS06UPG.BIN")
    bootloader = read(args.unpacked, "bootloader.bin")

    # ---- Validate headers ----
    MAGIC = b'\x45\x01\xfa\xeb\x13\x90'
    if not header1.startswith(MAGIC):
        print("Warning: header1.bin does not start with expected magic bytes", file=sys.stderr)
    if not header2.startswith(MAGIC):
        print("Warning: header2.bin does not start with expected magic bytes", file=sys.stderr)
    if len(header1) != HEADER1_SIZE:
        sys.exit(f"Error: header1.bin must be exactly {HEADER1_SIZE} bytes")
    if len(header2) != HEADER2_SIZE:
        sys.exit(f"Error: header2.bin must be exactly {HEADER2_SIZE} bytes")
    if len(bootloader) != BOOTLOADER_SIZE:
        sys.exit(f"Error: bootloader.bin must be exactly {BOOTLOADER_SIZE} bytes "
                 f"(got {len(bootloader)})")

    # ---- Compress components into ZIP archives ----
    print(f"Compressing PS06EPS.BIN  ({len(eps_bin)} bytes)…")
    zip1 = make_zip("PS06EPS.BIN", eps_bin)
    print(f"  compressed → {len(zip1)} bytes")

    print(f"Compressing PS06UPG.BIN  ({len(upg_bin)} bytes)…")
    zip2 = make_zip("PS06UPG.BIN", upg_bin)
    print(f"  compressed → {len(zip2)} bytes")

    # ---- Check layout fits ----
    zip1_end = ZIP1_OFFSET + len(zip1)
    if zip1_end > HEADER2_OFFSET:
        sys.exit(
            f"Error: ZIP1 ({len(zip1)} bytes) extends past HEADER2_OFFSET "
            f"(0x{HEADER2_OFFSET:x}).  PS06EPS.BIN is too large."
        )

    zip2_end = ZIP2_OFFSET + len(zip2)
    if zip2_end > BOOTLOADER_OFFSET:
        sys.exit(
            f"Error: ZIP2 ({len(zip2)} bytes) extends past BOOTLOADER_OFFSET "
            f"(0x{BOOTLOADER_OFFSET:x}).  PS06UPG.BIN is too large."
        )

    # ---- Assemble image ----
    # Start with a blank (zero-filled) 512 KB image
    image = bytearray(FIRMWARE_SIZE)

    # Header 1
    image[HEADER1_OFFSET: HEADER1_OFFSET + HEADER1_SIZE] = header1
    # ZIP1
    image[ZIP1_OFFSET: ZIP1_OFFSET + len(zip1)] = zip1
    # Header 2
    image[HEADER2_OFFSET: HEADER2_OFFSET + HEADER2_SIZE] = header2
    # ZIP2
    image[ZIP2_OFFSET: ZIP2_OFFSET + len(zip2)] = zip2
    # Bootloader (at end of flash)
    image[BOOTLOADER_OFFSET: BOOTLOADER_OFFSET + BOOTLOADER_SIZE] = bootloader

    # ---- Write output ----
    with open(args.output, "wb") as fh:
        fh.write(image)
    print(f"\nFirmware image written to '{args.output}' ({len(image)} bytes)")
    print("Flash via web interface: System → Upgrade → select the .bin file.")


if __name__ == "__main__":
    main()
