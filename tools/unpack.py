#!/usr/bin/env python3
"""
unpack.py — Extract components from a PS-1206U firmware image.

Usage:
    python3 tools/unpack.py PS-1206U_v8.8.bin [output_dir]

Output directory layout after unpacking:
    <output_dir>/
        header1.bin      — 32-byte "Edimax8820 MTYPE/" header that precedes ZIP1
        PS06EPS.BIN      — Main print-server firmware (x86 real-mode, RTXC RTOS)
        header2.bin      — 32-byte "Edimax7420 MTYPE/" header that precedes ZIP2
        PS06UPG.BIN      — Upgrade/bootloader utility
        bootloader.bin   — ROM bootloader code stored at the end of flash
        layout.txt       — Human-readable description of the firmware layout
"""

import argparse
import io
import os
import sys
import zipfile

FIRMWARE_SIZE   = 0x80000       # 512 KB — total flash size
HEADER1_OFFSET  = 0x00000
HEADER1_SIZE    = 32
ZIP1_OFFSET     = HEADER1_OFFSET + HEADER1_SIZE   # 0x00020
HEADER2_OFFSET  = 0x60000
HEADER2_SIZE    = 32
ZIP2_OFFSET     = HEADER2_OFFSET + HEADER2_SIZE   # 0x60020
BOOTLOADER_OFFSET = 0x78000     # Third code section (reset-vector bootstrap + loader)

MAGIC = b'\x45\x01\xfa\xeb\x13\x90'   # Common 6-byte prefix for both headers


def find_eocd(data: bytes, start: int) -> int:
    """Return the byte offset *just past* the ZIP End-Of-Central-Directory record.

    Scans forward from *start* looking for the PK\\x05\\x06 signature and
    validates that the record is well-formed and fits inside *data*.
    """
    pos = start
    while True:
        pos = data.find(b'PK\x05\x06', pos)
        if pos == -1:
            raise ValueError("ZIP End-of-Central-Directory record not found")
        # EOCD minimum size is 22 bytes; bytes 20-21 hold the comment length.
        if pos + 22 > len(data):
            pos += 1
            continue
        comment_len = int.from_bytes(data[pos + 20: pos + 22], 'little')
        eocd_end = pos + 22 + comment_len
        if eocd_end > len(data):
            pos += 1
            continue
        return eocd_end


def extract_zip(data: bytes, zip_start: int, zip_end: int):
    """Return a dict {filename: bytes} for each entry in the ZIP slice."""
    result = {}
    with zipfile.ZipFile(io.BytesIO(data[zip_start:zip_end])) as zf:
        for name in zf.namelist():
            result[name] = zf.read(name)
    return result


def main():
    parser = argparse.ArgumentParser(description="Unpack a PS-1206U firmware .bin file")
    parser.add_argument("firmware", help="Path to PS-1206U_v8.8.bin (or compatible)")
    parser.add_argument("output", nargs="?", default="firmware_unpacked",
                        help="Output directory (default: firmware_unpacked)")
    args = parser.parse_args()

    with open(args.firmware, "rb") as fh:
        data = fh.read()

    if len(data) != FIRMWARE_SIZE:
        print(f"Warning: expected {FIRMWARE_SIZE} bytes, got {len(data)} bytes", file=sys.stderr)

    # --- Header 1 ---
    header1 = data[HEADER1_OFFSET: HEADER1_OFFSET + HEADER1_SIZE]
    if not header1.startswith(MAGIC):
        print("Warning: Header 1 magic mismatch", file=sys.stderr)
    label1 = header1[6:23]
    print(f"Header 1 label : {label1.decode('latin-1')!r}")

    # --- ZIP 1 ---
    zip1_end = find_eocd(data, ZIP1_OFFSET)
    print(f"ZIP1            : 0x{ZIP1_OFFSET:05x} – 0x{zip1_end:05x}  ({zip1_end - ZIP1_OFFSET} bytes)")
    zip1_contents = extract_zip(data, ZIP1_OFFSET, zip1_end)
    for name, content in zip1_contents.items():
        print(f"  -> {name}: {len(content)} bytes")

    # --- Header 2 ---
    header2 = data[HEADER2_OFFSET: HEADER2_OFFSET + HEADER2_SIZE]
    if not header2.startswith(MAGIC):
        print("Warning: Header 2 magic mismatch", file=sys.stderr)
    label2 = header2[6:23]
    print(f"Header 2 label : {label2.decode('latin-1')!r}")

    # --- ZIP 2 ---
    zip2_end = find_eocd(data, ZIP2_OFFSET)
    print(f"ZIP2            : 0x{ZIP2_OFFSET:05x} – 0x{zip2_end:05x}  ({zip2_end - ZIP2_OFFSET} bytes)")
    zip2_contents = extract_zip(data, ZIP2_OFFSET, zip2_end)
    for name, content in zip2_contents.items():
        print(f"  -> {name}: {len(content)} bytes")

    # --- Bootloader / ROM section ---
    bootloader = data[BOOTLOADER_OFFSET: FIRMWARE_SIZE]
    print(f"Bootloader      : 0x{BOOTLOADER_OFFSET:05x} – 0x{FIRMWARE_SIZE:05x}  ({len(bootloader)} bytes)")

    # --- Write output ---
    os.makedirs(args.output, exist_ok=True)

    def write(filename, content):
        path = os.path.join(args.output, filename)
        with open(path, "wb") as fh:
            fh.write(content)
        print(f"Written: {path}")

    write("header1.bin", header1)
    for name, content in zip1_contents.items():
        write(name, content)

    write("header2.bin", header2)
    for name, content in zip2_contents.items():
        write(name, content)

    write("bootloader.bin", bootloader)

    # --- Write layout description ---
    layout = (
        f"PS-1206U firmware layout\n"
        f"========================\n"
        f"Total size       : {FIRMWARE_SIZE} bytes (0x{FIRMWARE_SIZE:x})\n"
        f"\n"
        f"Offset     Size      Description\n"
        f"---------- --------- -----------\n"
        f"0x{HEADER1_OFFSET:05x}      {HEADER1_SIZE:<9} Header 1 ({label1.decode('latin-1')})\n"
        f"0x{ZIP1_OFFSET:05x}   {zip1_end - ZIP1_OFFSET:<9} ZIP archive containing PS06EPS.BIN\n"
        f"0x{zip1_end:05x}   {HEADER2_OFFSET - zip1_end:<9} Zero padding\n"
        f"0x{HEADER2_OFFSET:05x}      {HEADER2_SIZE:<9} Header 2 ({label2.decode('latin-1')})\n"
        f"0x{ZIP2_OFFSET:05x}   {zip2_end - ZIP2_OFFSET:<9} ZIP archive containing PS06UPG.BIN\n"
        f"0x{zip2_end:05x}   {BOOTLOADER_OFFSET - zip2_end:<9} Zero padding\n"
        f"0x{BOOTLOADER_OFFSET:05x}   {FIRMWARE_SIZE - BOOTLOADER_OFFSET:<9} Bootloader / ROM section\n"
        f"\n"
        f"PS06EPS.BIN  : main print-server firmware (x86 real-mode, RTXC RTOS)\n"
        f"              Contains embedded HTML/CSS/JS web UI, JPEG images, IPP\n"
        f"              server, LPD, SMB, AppleTalk/PAP stacks.\n"
        f"              To analyse the code use a disassembler that supports\n"
        f"              16-bit x86 (e.g. Ghidra with x86:LE:16:default language,\n"
        f"              or IDA Pro / Binary Ninja with 8086 support).\n"
        f"\n"
        f"PS06UPG.BIN  : upgrade utility / second-stage bootloader\n"
        f"              Also x86 real-mode code.\n"
        f"\n"
        f"bootloader.bin : ROM-resident first-stage bootloader + reset vector\n"
        f"              The x86 reset vector at 0x7ff00 contains a far jump\n"
        f"              (JMP 0xfffc:0x0000) that starts execution here after\n"
        f"              power-on.  Do not modify unless you know what you are\n"
        f"              doing — a broken bootloader will hard-brick the device.\n"
    )
    layout_path = os.path.join(args.output, "layout.txt")
    with open(layout_path, "w") as fh:
        fh.write(layout)
    print(f"Written: {layout_path}")
    print("\nDone.  Use tools/repack.py to rebuild the firmware after making changes.")


if __name__ == "__main__":
    main()
