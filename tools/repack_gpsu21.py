#!/usr/bin/env python3
"""
repack_gpsu21.py - Repack modified web resources into an IOGear GPSU21 firmware image.

This is the companion to unpack_gpsu21.py.  After extracting the firmware with
unpack_gpsu21.py, editing the HTML/JS/CSS files, run this script to produce a
new flashable .bin image.

Usage:
    python  tools/repack_gpsu21.py  <original.bin>  <edited_dir/>  <output.bin>
    python3 tools/repack_gpsu21.py  MPS56_90956F_9034_20191119.bin  gpsu21_extracted/  gpsu21_modified.bin

    original.bin  — the original, unmodified firmware .bin (or .zip wrapping it)
    edited_dir/   — the directory produced by unpack_gpsu21.py (must contain manifest.txt)
    output.bin    — where to write the new flashable image

IMPORTANT — size constraints
──────────────────────────────
Each modified file MUST be the same size or SMALLER than the original.
The eCos binary is a single flat image; file data is located at fixed offsets
within it.  If a file grows beyond its original allocation there is no room for
it in the binary and repacking will abort.

To shorten a file without changing its size on disk, pad it to the original size
with whitespace or HTML comments.  Example:

    <!-- padding to fill original 1,981 bytes:
         xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx -->

Workflow summary:
    1.  unpack_gpsu21.py  →  extract all files to a directory
    2.  Edit HTML/JS/CSS files in that directory (do NOT change file sizes beyond
        the originals; pad with spaces/comments if needed)
    3.  repack_gpsu21.py  →  produces a new .bin ready to flash
    4.  Flash via the GPSU21 web interface  (System → Upgrade → browse for .bin)
"""

import sys
import os
import lzma
import struct
import zipfile
import zlib


LZMA_OFFSET  = 0x4AC0
UIMAGE_OFFSET = 0x100

# U-Boot uImage header field offsets
UIMAGE_CRC_OFFSET  = 4   # header CRC32 (we will recalculate)
UIMAGE_DCRC_OFFSET = 24  # data CRC32


def _load_firmware(path):
    if path.lower().endswith(".zip"):
        with zipfile.ZipFile(path) as zf:
            names = [n for n in zf.namelist() if n.lower().endswith(".bin")]
            if not names:
                raise ValueError(f"No .bin file found inside {path}")
            return zf.read(names[0]), names[0]
    with open(path, "rb") as f:
        return f.read(), os.path.basename(path)


def _crc32(data):
    """Standard CRC-32 (same as used by U-Boot)."""
    return zlib.crc32(data) & 0xFFFFFFFF


def _patch_uimage_crc(fw_bytes, new_payload_bytes):
    """
    Rebuild the uImage header at UIMAGE_OFFSET with corrected CRC fields.

    The uImage wraps everything from UIMAGE_OFFSET+64 to end-of-file.
    We recalculate:
      - data CRC32  (covers the new compressed payload)
      - header CRC32 (covers the 64-byte header with data CRC filled in, header CRC zeroed)
    """
    hdr = bytearray(fw_bytes[UIMAGE_OFFSET: UIMAGE_OFFSET + 64])

    # Update data size field
    struct.pack_into(">I", hdr, 12, len(new_payload_bytes))

    # Recalculate data CRC32
    dcrc = _crc32(new_payload_bytes)
    struct.pack_into(">I", hdr, UIMAGE_DCRC_OFFSET, dcrc)

    # Zero out header CRC, then calculate it
    struct.pack_into(">I", hdr, UIMAGE_CRC_OFFSET, 0)
    hcrc = _crc32(bytes(hdr))
    struct.pack_into(">I", hdr, UIMAGE_CRC_OFFSET, hcrc)

    return bytes(hdr)


def repack(original_path, edited_dir, output_path):
    print(f"Loading original firmware {original_path} …")
    fw, orig_name = _load_firmware(original_path)
    print(f"  Original size: {len(fw):,} bytes")

    print("Decompressing LZMA payload …")
    original_lzma_stream = fw[LZMA_OFFSET:]
    raw = bytearray(lzma.decompress(original_lzma_stream))
    print(f"  Decompressed:  {len(raw):,} bytes")

    # Read manifest
    manifest_path = os.path.join(edited_dir, "manifest.txt")
    if not os.path.exists(manifest_path):
        raise FileNotFoundError(
            f"manifest.txt not found in {edited_dir!r}.\n"
            "Run unpack_gpsu21.py first."
        )

    entries = []
    with open(manifest_path) as mf:
        for line in mf:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("|")
            if len(parts) != 4:
                continue
            subdir, name, abs_offset_hex, size_str = parts
            entries.append((subdir, name, int(abs_offset_hex, 16), int(size_str)))

    print(f"\nPatching {len(entries)} files …\n")
    patched = 0
    skipped = 0
    errors  = 0

    for subdir, name, abs_offset, original_size in entries:
        edited_path = os.path.join(edited_dir, subdir, name)
        if not os.path.exists(edited_path):
            print(f"  SKIP  {subdir}{name} (not found in edited_dir)")
            skipped += 1
            continue

        with open(edited_path, "rb") as f:
            new_data = f.read()

        if len(new_data) > original_size:
            print(
                f"  ERROR {subdir}{name}: edited size {len(new_data):,} > "
                f"original {original_size:,} bytes — SKIPPED (must pad to fit)"
            )
            errors += 1
            continue

        # Pad with null bytes if shorter
        if len(new_data) < original_size:
            ext = os.path.splitext(name)[1].lower()
            # For text files pad with spaces; for binary use null bytes
            pad_byte = b" " if ext in (".htm", ".html", ".js", ".css") else b"\x00"
            new_data = new_data + pad_byte * (original_size - len(new_data))

        raw[abs_offset : abs_offset + original_size] = new_data

        actual = bytes(raw[abs_offset : abs_offset + original_size])
        changed = "CHANGED" if actual != bytes(lzma.decompress(original_lzma_stream)[abs_offset : abs_offset + original_size]) else "same"
        print(f"  {('MOD' if changed == 'CHANGED' else 'same'):<5} {subdir}{name:<40} {original_size:,} bytes")
        if changed == "CHANGED":
            patched += 1

    print(f"\n{patched} files modified, {skipped} skipped, {errors} errors")
    if errors:
        print(f"WARNING: {errors} file(s) were too large and could not be patched.")

    # Recompress with LZMA
    print("\nRecompressing LZMA payload …")
    # Use the same LZMA properties as the original:
    # dict_size=8MB (0x800000), lc=3, lp=0, pb=2 (standard LZMA properties byte 0x5d)
    filters = [
        {
            "id": lzma.FILTER_LZMA1,
            "dict_size": 8 * 1024 * 1024,
            "lc": 3,
            "lp": 0,
            "pb": 2,
            "preset": lzma.PRESET_DEFAULT,
        }
    ]
    new_lzma = lzma.compress(bytes(raw), format=lzma.FORMAT_ALONE, filters=filters)
    print(f"  New compressed size: {len(new_lzma):,} bytes  "
          f"(original: {len(original_lzma_stream):,})")

    # Rebuild firmware:  ZOT header (0x0000-0x00FF)
    #                  + uImage header (0x0100-0x013F, with updated CRCs)
    #                  + padding (0x0140-0x4ABF, unchanged)
    #                  + new LZMA payload
    zot_header   = fw[:UIMAGE_OFFSET]            # 256 bytes
    padding      = fw[UIMAGE_OFFSET + 64 : LZMA_OFFSET]  # gap between uImage hdr and LZMA

    new_uimage_hdr = _patch_uimage_crc(fw, new_lzma)
    new_fw = bytearray(zot_header + new_uimage_hdr + padding + new_lzma)

    # Update ZOT header payload-size field (bytes 8–11, LE uint32)
    # The field holds the number of bytes after the 256-byte ZOT header.
    struct.pack_into("<I", new_fw, 8, len(new_fw) - UIMAGE_OFFSET)

    # Recalculate ZOT header checksum (bytes 0–3, LE uint32).
    # The device stores the bitwise complement of CRC32(fw[4:]).
    zot_crc = zlib.crc32(bytes(new_fw[4:])) ^ 0xFFFFFFFF
    struct.pack_into("<I", new_fw, 0, zot_crc)

    new_fw = bytes(new_fw)

    print(f"  New firmware size:   {len(new_fw):,} bytes  "
          f"(original: {len(fw):,})")

    with open(output_path, "wb") as f:
        f.write(new_fw)

    print(f"\nWrote modified firmware → {output_path}")
    print()
    print("Flash via the GPSU21 web interface:")
    print("  1. Open http://<printer-ip>/ in a browser")
    print("  2. Navigate to the firmware upgrade page")
    print(f"  3. Upload {output_path}")
    print("  4. Do NOT power-cycle during the upgrade")


# ──────────────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(1)
    repack(sys.argv[1], sys.argv[2], sys.argv[3])
