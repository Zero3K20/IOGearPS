#!/usr/bin/env python3
"""
unpack_gpsu21.py - Extract web resources from an IOGear GPSU21 firmware image.

The GPSU21 firmware uses eCos RTOS on a MediaTek MT7688 (MIPS) SoC.  All web
interface files (HTML pages, JavaScript, CSS, and JPEG/GIF images) are stored
as raw, uncompressed data inside the LZMA-compressed eCos application binary.

Usage:
    python  tools/unpack_gpsu21.py  <firmware.bin>  <output_dir/>
    python3 tools/unpack_gpsu21.py  MPS56_90956F_9034_20191119.bin  gpsu21_extracted/

If the input is still inside a .zip archive, extract it first:
    # Windows:  expand the .zip in Explorer, then pass the .bin file
    # Linux:    unzip MPS56_90956F_9034_20191119.zip

Repack after editing:
    python tools/repack_gpsu21.py  <original.bin>  <edited_dir/>  <output.bin>

Firmware layout (MPS56_90956F_9034_20191119.bin, 350,428 bytes):
    0x0000 - 0x00FF   256-byte ZOT Technology firmware header
                      Contains version string: "MT7688-9.09.56.9034.00001243t-..."
    0x0100 - 0x013F   U-Boot uImage header (name "zot716u2", MIPS, standalone)
    0x0140 - 0x4ABF   Stage-2 MIPS bootstrap code (NOT padding — the ZOT bootloader
                      copies this region to DRAM at 0x80500000 and jumps there before
                      the LZMA payload is decompressed; see package_firmware.py)
    0x4AC0 - end      LZMA-compressed eCos application image (decompresses to ~1.53 MB)

Decompressed eCos image layout:
    0x000000 - 0x0EFFFF   eCos kernel, application code, MIPS machine code
    0x0F0000 - 0x0F02C5   ROM file table — string constants used by the web UI
    0x0F02C6 onwards      ROM file data section (RESOURCES_BASE)
                          File entries in the table above reference this base.
    0x128B7C              Image file sub-table (JPEG/GIF files)

ROM file table entry format (each web resource except the last one):
    <filename>\\x00  (null-terminated, 1-30 chars)
    offset  (LE uint32) — byte offset from RESOURCES_BASE to start of file data
    size    (LE uint32) — byte length of file data
    type    (LE uint32) — internal MIME-type/flags index (not needed for extraction)
"""

import sys
import os
import lzma
import struct
import zipfile


# ──────────────────────────────────────────────────────────────────────────────
# Firmware constants (all offsets are within the raw .bin file unless noted)
# ──────────────────────────────────────────────────────────────────────────────

LZMA_OFFSET    = 0x4AC0      # byte offset of the LZMA stream in the .bin file

# Offsets within the DECOMPRESSED eCos image:
TABLE_SCAN_START = 0x0F0000  # start of the ROM file table region
TABLE_SCAN_END   = 0x0F06F0  # end of the main file table (top.js inline here)
RESOURCES_BASE   = 0x0F02C6  # base address for all file offsets in the table

IMAGE_TABLE_SCAN_START = 0x128B7C  # start of image sub-table
IMAGE_TABLE_SCAN_END   = 0x128E00  # end of image sub-table

# ──────────────────────────────────────────────────────────────────────────────

VALID_NAME_CHARS = set(
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-"
)


def _load_firmware(path):
    """Return the raw bytes of the firmware .bin file, unwrapping .zip if needed."""
    if path.lower().endswith(".zip"):
        with zipfile.ZipFile(path) as zf:
            names = [n for n in zf.namelist() if n.lower().endswith(".bin")]
            if not names:
                raise ValueError(f"No .bin file found inside {path}")
            if len(names) > 1:
                print(f"  Note: multiple .bin files in zip, using {names[0]!r}")
            return zf.read(names[0])
    with open(path, "rb") as f:
        return f.read()


def _decompress(fw):
    """Decompress the LZMA payload embedded in the firmware binary."""
    return lzma.decompress(fw[LZMA_OFFSET:])


def _parse_table(raw, scan_start, scan_end, subdir):
    """
    Parse a ROM file table region and return a list of
    (subdir, name, abs_offset, size) tuples.

    abs_offset is the absolute byte offset within `raw` where the file data
    starts (i.e. RESOURCES_BASE + entry_offset).
    """
    entries = []
    i = scan_start
    while i < scan_end:
        null_pos = raw.find(b"\x00", i)
        if null_pos == -1 or null_pos >= scan_end:
            break
        name_bytes = raw[i:null_pos]
        try:
            name = name_bytes.decode("latin-1")
        except Exception:
            i += 1
            continue

        # A valid filename: contains a dot, reasonable length, safe characters
        if (
            "." in name
            and 2 <= len(name) <= 40
            and all(c in VALID_NAME_CHARS for c in name)
        ):
            meta = null_pos + 1
            if meta + 12 <= len(raw):
                entry_offset = struct.unpack_from("<I", raw, meta)[0]
                size         = struct.unpack_from("<I", raw, meta + 4)[0]
                if 0 < size < 0x800000 and entry_offset < 0x600000:
                    abs_offset = RESOURCES_BASE + entry_offset
                    if abs_offset + size <= len(raw):
                        entries.append((subdir, name, abs_offset, size))
                        i = meta + 12
                        continue
        i += 1
    return entries


def extract(firmware_path, output_dir):
    """Extract all web resources from *firmware_path* into *output_dir*."""
    print(f"Loading {firmware_path} …")
    fw = _load_firmware(firmware_path)
    print(f"  Firmware size: {len(fw):,} bytes")

    print("Decompressing LZMA payload …")
    raw = _decompress(fw)
    print(f"  Decompressed:  {len(raw):,} bytes")

    # Extract version string from ZOT header
    ver_bytes = fw[0x28:0x5B]
    try:
        ver = ver_bytes.rstrip(b"\x00").decode("latin-1")
        print(f"  Firmware version: {ver}")
    except Exception:
        pass

    # Parse the two file tables
    entries = _parse_table(raw, TABLE_SCAN_START, TABLE_SCAN_END, "")
    img_entries = _parse_table(
        raw, IMAGE_TABLE_SCAN_START, IMAGE_TABLE_SCAN_END, "images/"
    )
    all_entries = entries + img_entries

    print(f"\nFound {len(all_entries)} files "
          f"({len(entries)} web pages/scripts/CSS, {len(img_entries)} images)\n")

    os.makedirs(output_dir, exist_ok=True)
    os.makedirs(os.path.join(output_dir, "images"), exist_ok=True)

    written = 0
    for subdir, name, abs_offset, size in sorted(
        all_entries, key=lambda x: (x[0], x[1])
    ):
        data = raw[abs_offset : abs_offset + size]
        rel_path = os.path.join(subdir, name)
        out_path = os.path.join(output_dir, rel_path)
        with open(out_path, "wb") as f:
            f.write(data)
        ext = os.path.splitext(name)[1].lower()
        preview = ""
        if ext in (".htm", ".html", ".js", ".css"):
            try:
                preview = "  " + data[:60].decode("latin-1").strip()[:55]
            except Exception:
                pass
        print(f"  {rel_path:<40}  {size:7,} bytes{preview}")
        written += 1

    # Write a manifest so repack_gpsu21.py can find everything
    manifest_path = os.path.join(output_dir, "manifest.txt")
    with open(manifest_path, "w") as mf:
        mf.write("# GPSU21 ROM file manifest — generated by unpack_gpsu21.py\n")
        mf.write("# Format: subdir|name|abs_offset_hex|size_decimal\n")
        for subdir, name, abs_offset, size in all_entries:
            mf.write(f"{subdir}|{name}|0x{abs_offset:x}|{size}\n")

    print(f"\nExtracted {written} files → {output_dir}/")
    print(f"Manifest written to {manifest_path}")
    print()
    print("Edit files as needed, then repack with:")
    print(f"  python tools/repack_gpsu21.py  {firmware_path}  {output_dir}/  modified.bin")


# ──────────────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    extract(sys.argv[1], sys.argv[2])
