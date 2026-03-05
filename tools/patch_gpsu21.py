#!/usr/bin/env python3
"""
patch_gpsu21.py - Apply all confirmed bug-fix binary patches to an IOGear GPSU21
firmware image, then produce a new flashable .bin file.

Bugs fixed by this tool
───────────────────────
  1. Bonjour / mDNS lock counter de-sync  (mDNS_Lock + mDNS_Unlock)
       Root cause: when mDNS_busy != mDNS_reentrancy (e.g. after a transient
       interrupt racing the lock), the firmware only logs the error and
       continues — but the counters stay permanently out of sync, so
       mDNSCoreTask() is never triggered again and Bonjour advertising stops.
       Fix: in the error branch of both mDNS_Lock and mDNS_Unlock, reset
       mDNS_reentrancy = mDNS_busy before proceeding, so the counters are
       always in sync after the lock returns.

  2. eCos thread wakeup_count overflow assertion
       Root cause: cyg_thread_wakeup() asserts (halt / fatal error) if a
       thread accumulates ≥ 500 pending wakeup requests.  Under heavy Bonjour
       traffic the mDNS thread can hit this limit, causing the firmware to
       reset or stall.
       Fix: remove the branch that leads to the assert, so wakeup_count simply
       wraps instead of triggering a fatal assertion.

  3. Browser auto-refresh causing HTTP connection exhaustion
       Root cause: INDEX.HTM contains a 10-second and SERVICES.HTM a 5-second
       <meta refresh> tag.  Every browser watching the status page opens new
       TCP connections, eventually hitting the 36-connection HTTP pool limit
       which then logs "No free socket!" and blocks all further connections.
       Fix: replace both refresh tags with whitespace (same byte length, so
       the flash layout is unchanged).

Supported firmware
──────────────────
  Build 9034 — MPS56_90956F_9034_20191119.bin  (2019 ZOT OEM)
  Build 9032 — MPS56_IOG_GPSU21_20171123.bin   (2017 IOGear retail)
  Other builds of the same firmware family should also work because the tool
  locates all patch sites dynamically from known strings.

Usage
─────
    python3 tools/patch_gpsu21.py  <input>   <output.bin>

    input  — the firmware .bin file, or a .zip archive that contains it
    output — where to write the patched firmware (must end in .bin)

Examples
────────
    # 2019 ZOT firmware (may be inside a ZIP):
    python3 tools/patch_gpsu21.py  MPS56_90956F_9034_20191119.zip  fixed.bin

    # 2017 IOGear firmware:
    python3 tools/patch_gpsu21.py  MPS56_IOG_GPSU21_20171123.zip   fixed.bin

After patching
──────────────
    1. Open http://<printer-ip>/ in a browser
    2. Navigate to the firmware upgrade page (varies by firmware build)
    3. Upload the output .bin file
    4. Do NOT power-cycle during the upgrade (~60 s)
"""

import sys
import os
import re
import lzma
import struct
import zipfile
import zlib


# ──────────────────────────────────────────────────────────────────────────────
# Firmware layout constants (all offsets within the raw .bin file)
# ──────────────────────────────────────────────────────────────────────────────

LZMA_OFFSET    = 0x4AC0   # start of LZMA stream in the .bin file
UIMAGE_OFFSET  = 0x0100   # start of 64-byte U-Boot uImage header

# MIPS load address of the decompressed eCos image
MIPS_BASE = 0x80000400

# ──────────────────────────────────────────────────────────────────────────────
# Helper: MIPS instruction encoding / decoding
# ──────────────────────────────────────────────────────────────────────────────

def _lw(rt, offset, rs):
    """Encode MIPS LW rt, offset(rs) as LE bytes."""
    word = (0x23 << 26) | (rs << 21) | (rt << 16) | (offset & 0xFFFF)
    return struct.pack("<I", word)

def _sw(rt, offset, rs):
    """Encode MIPS SW rt, offset(rs) as LE bytes."""
    word = (0x2B << 26) | (rs << 21) | (rt << 16) | (offset & 0xFFFF)
    return struct.pack("<I", word)

_NOP = b"\x00\x00\x00\x00"

# MIPS register numbers
_ZERO, _V0, _V1, _A0, _A1, _A2, _A3 = 0, 2, 3, 4, 5, 6, 7
_SP = 29


# ──────────────────────────────────────────────────────────────────────────────
# Core helper: find all (file-offset) positions of a LUI/ADDIU pair that
# loads exactly *target_va* into any register.
# ──────────────────────────────────────────────────────────────────────────────

def _find_lui_immediate_pair(raw, target_va):
    """
    Search *raw* (decompressed eCos image, bytes) for MIPS LUI/ADDIU or
    LUI/ORI instruction pairs that load exactly *target_va*.

    For a target whose low 16 bits are >= 0x8000 the compiler emits
    LUI with hi+1 and then ADDIU with the sign-negative low half; the
    ORI variant is used when the low half is non-negative.  Both cases
    are handled.

    Returns a list of file offsets of the LUI instruction.
    """
    hi = (target_va >> 16) & 0xFFFF
    lo = target_va & 0xFFFF
    lo_s = lo - 0x10000 if lo >= 0x8000 else lo
    # When lo is sign-negative the LUI immediate must be hi+1 so that
    # LUI*2^16 + sign_extend(lo) == target_va.
    lui_hi = (hi + 1) & 0xFFFF if lo >= 0x8000 else hi

    results = []
    for i in range(0, len(raw) - 4, 4):
        word = struct.unpack_from("<I", raw, i)[0]
        op   = (word >> 26) & 0x3F
        rt   = (word >> 16) & 0x1F
        imm  = word & 0xFFFF
        if op == 0xF and imm == lui_hi:          # LUI rX, lui_hi
            for j in range(4, 80, 4):
                ni = i + j
                if ni + 4 > len(raw):
                    break
                nw   = struct.unpack_from("<I", raw, ni)[0]
                nop  = (nw >> 26) & 0x3F
                nrs  = (nw >> 21) & 0x1F
                # nim is the raw unsigned 16-bit field; compare with lo directly
                # because the encoding stores the 2's-complement value as-is.
                nim  = nw & 0xFFFF
                nim_s = nim - 0x10000 if nim >= 0x8000 else nim
                if nop in (9, 0xD) and nrs == rt and nim == lo:
                    computed = ((lui_hi << 16) + nim_s) & 0xFFFFFFFF
                    if computed == target_va:
                        results.append(i)
                    break
    return results


# ──────────────────────────────────────────────────────────────────────────────
# Patch 1 & 2: mDNS lock counter resync
# ──────────────────────────────────────────────────────────────────────────────
#
# In Apple mDNSCore the mDNS_Lock / mDNS_Unlock functions guard a "locking
# failure" check:
#
#   if (m->mDNS_busy != m->mDNS_reentrancy)
#       LogMsg("mDNS_Lock: Locking failure! ...");
#   m->mDNS_busy++;
#
# In the MIPS binary the compiler emits exactly this sequence when the
# condition is TRUE (counters disagree):
#
#   lw  $v0, X($sp)          // reload m
#   lw  $v1, 0x18($v0)       // v1 = m->mDNS_busy
#   lw  $v0, X($sp)          // reload m
#   lw  $a2, 0x1c($v0)       // a2 = m->mDNS_reentrancy
#   lui $v0, <string_hi>
#   addiu $a0, $v0, <string_lo>
#   move $a1, $v1
#   jal  LogMsg
#   nop
#
# We replace instructions 4-9 (the LogMsg call) with a store that writes
# mDNS_busy back into mDNS_reentrancy, then 5 × nop.  This keeps the
# counters in sync without any externally visible side-effect.
#
# The same pattern appears in mDNS_Unlock (different string address).
#
# Struct field offsets (same in all builds):
#   m->mDNS_busy       at  0x18  (byte offset from the mDNS struct pointer)
#   m->mDNS_reentrancy at  0x1c

_MDNS_LOCK_STR   = b"mDNS_Lock: Locking failure!"
_MDNS_UNLOCK_STR = b"mDNS_Unlock: Locking failure!"

# Expected instruction bytes immediately before the LUI (4 instructions = 16 bytes)
# that the search must confirm to avoid false positives.
# Pattern: lw v0,X(sp)  lw v1,0x18(v0)  lw v0,X(sp)  lw a2,0x1c(v0)
_LW_V1_0x18_V0  = struct.pack("<I", (0x23 << 26) | (_V0 << 21) | (_V1 << 16) | 0x18)  # 8c430018
_LW_A2_0x1C_V0  = struct.pack("<I", (0x23 << 26) | (_V0 << 21) | (_A2 << 16) | 0x1C)  # 8c46001c
_MOVE_A1_V1     = struct.pack("<I", (0x00 << 26) | (_V1 << 21) | (_A1 << 11) | 0x21)  # 00602821


def _find_mdns_error_block(raw, str_bytes):
    """
    Locate the 9-instruction (36-byte) error block for mDNS_Lock or mDNS_Unlock.

    The error block starts 4 instructions (16 bytes) before the LUI that loads
    the failure-string address.  We verify the surrounding instruction pattern
    before returning the offset.

    Returns (file_offset, sp_field) where sp_field is the stack offset used by
    lw $v0, X($sp) in the error block, or raises RuntimeError if not found.
    """
    str_off = raw.find(str_bytes)
    if str_off < 0:
        raise RuntimeError(f"Could not find string {str_bytes[:30]!r} in firmware")

    str_va = MIPS_BASE + str_off
    lui_offsets = _find_lui_immediate_pair(raw, str_va)
    if not lui_offsets:
        raise RuntimeError(
            f"No LUI/ADDIU reference to 0x{str_va:08x} "
            f"({str_bytes[:30]!r}) found in code"
        )

    for lui_off in lui_offsets:
        block_start = lui_off - 16   # 4 instructions before LUI
        if block_start < 0:
            continue

        # Read the 4 instructions that should precede the LUI
        i0 = struct.unpack_from("<I", raw, block_start)[0]      # lw v0, X(sp)
        i1 = struct.unpack_from("<I", raw, block_start + 4)[0]  # lw v1, 0x18(v0)
        i2 = struct.unpack_from("<I", raw, block_start + 8)[0]  # lw v0, X(sp)
        i3 = struct.unpack_from("<I", raw, block_start + 12)[0] # lw a2, 0x1c(v0)

        # i0 and i2: lw $v0, X($sp) — op=0x23, rs=sp, rt=v0
        def is_lw_v0_sp(w):
            return ((w >> 26) == 0x23 and
                    ((w >> 21) & 0x1F) == _SP and
                    ((w >> 16) & 0x1F) == _V0)

        if not (is_lw_v0_sp(i0) and is_lw_v0_sp(i2)):
            continue
        # Verify both use the same stack offset (the 'm' pointer slot)
        sp_field_0 = i0 & 0xFFFF
        sp_field_2 = i2 & 0xFFFF
        if sp_field_0 != sp_field_2:
            continue

        # i1: lw $v1, 0x18($v0)
        if struct.pack("<I", i1) != _LW_V1_0x18_V0:
            continue

        # i3: lw $a2, 0x1c($v0)
        if struct.pack("<I", i3) != _LW_A2_0x1C_V0:
            continue

        # Verify move $a1, $v1 two instructions after LUI (= block_start + 24)
        move_off = block_start + 24
        if struct.pack("<I", struct.unpack_from("<I", raw, move_off)[0]) != _MOVE_A1_V1:
            continue

        return block_start, sp_field_0

    raise RuntimeError(
        f"Could not locate mDNS error block near string {str_bytes[:30]!r}"
    )


def _build_mdns_sync_patch(sp_field):
    """
    Build the 36-byte (9-instruction) replacement for the mDNS error block.

    The patch:
      lw  $v0, sp_field($sp)   ← reload m pointer
      lw  $v1, 0x18($v0)       ← v1 = m->mDNS_busy
      lw  $v0, sp_field($sp)   ← reload m pointer
      sw  $v1, 0x1c($v0)       ← m->mDNS_reentrancy = m->mDNS_busy  (THE FIX)
      nop × 5
    """
    return (
        _lw(_V0, sp_field, _SP) +   # lw $v0, sp_field($sp)
        _lw(_V1, 0x18, _V0)   +   # lw $v1, 0x18($v0)   [mDNS_busy]
        _lw(_V0, sp_field, _SP) +   # lw $v0, sp_field($sp)
        _sw(_V1, 0x1C, _V0)   +   # sw $v1, 0x1c($v0)   [mDNS_reentrancy = busy]
        _NOP * 5
    )


# ──────────────────────────────────────────────────────────────────────────────
# Patch 3: eCos wakeup_count overflow assertion bypass
# ──────────────────────────────────────────────────────────────────────────────
#
# cyg_thread_wakeup() in the eCos kernel contains:
#
#   thread->wakeup_count++;
#   if (thread->wakeup_count >= 500) {
#       CYG_ASSERT(false, "wakeup_count overflow");  // fatal halt!
#   }
#
# In the MIPS binary this compiles to:
#
#   lw   $v0, 0x50($a0)        // load wakeup_count
#   addiu $v0, $v0, 1          // increment
#   sltiu $v1, $v0, 500        // v1 = (count < 500)
#   beqz  $v1, <assert>        // if count >= 500  →  assert  ← PATCH: NOP
#   sw    $v0, 0x50($a0)       // store (delay slot, always executes)
#
# Replacing the beqz with a NOP makes the counter simply continue incrementing
# (it wraps at 2^32) instead of triggering the fatal assertion.
#
# We locate the patch site by searching for the unique 4-instruction sequence:
#   sltiu $v1, $v0, _WAKEUP_COUNT_THRESHOLD
#   beqz  $v1, ...
#   sw    $v0, 0x50($a0)
#
# Instruction encodings:
#   sltiu $v1, $v0, 0x1f4  →  2c4301f4  →  bytes f401432c
#   sw    $v0, 0x50($a0)   →  ac820050  →  bytes 500082ac

_WAKEUP_COUNT_THRESHOLD = 500   # eCos assert fires when wakeup_count >= this

_SLTIU_V1_V0_500 = struct.pack(
    "<I", (0x0B << 26) | (_V0 << 21) | (_V1 << 16) | _WAKEUP_COUNT_THRESHOLD
)
_SW_V0_0x50_A0   = struct.pack("<I", (0x2B << 26) | (_A0 << 21) | (_V0 << 16) | 0x50)


def _find_wakeup_overflow_beqz(raw):
    """
    Locate the BEQZ instruction that branches to the wakeup_count assert.

    The eCos kernel asserts when wakeup_count reaches _WAKEUP_COUNT_THRESHOLD
    (500).  We identify the site by searching for the unique instruction
    triple: SLTIU(v1, v0, threshold), BEQZ(v1, ...), SW(v0, 0x50, a0).

    Returns the file offset of the BEQZ, or raises RuntimeError.
    """
    # Search for the SLTIU followed within 1 instruction by SW $v0,0x50($a0)
    # (the BEQZ is between them — its delay slot IS the SW)
    for i in range(0, len(raw) - 12, 4):
        if raw[i:i+4] != _SLTIU_V1_V0_500:
            continue
        # Instruction at i+4 is the BEQZ (or possibly BNEZ)
        # Instruction at i+8 is the delay slot = SW $v0, 0x50($a0)
        if raw[i+8:i+12] != _SW_V0_0x50_A0:
            continue
        # Confirm the branch at i+4 uses $v1 as the test register
        branch_word = struct.unpack_from("<I", raw, i + 4)[0]
        branch_op = (branch_word >> 26) & 0x3F
        branch_rs = (branch_word >> 21) & 0x1F
        if branch_op == 4 and branch_rs == _V1:   # BEQ $v1, $zero, target
            return i + 4
        if branch_op == 5 and branch_rs == _V1:   # BNE (shouldn't happen, but safe)
            continue

    raise RuntimeError(
        "Could not locate wakeup_count overflow BEQZ instruction.\n"
        "The firmware may have a different code layout than expected."
    )


# ──────────────────────────────────────────────────────────────────────────────
# Patch 4: Remove HTML auto-refresh tags
# ──────────────────────────────────────────────────────────────────────────────
#
# Two pages carry periodic <meta http-equiv="refresh"> tags that cause browsers
# to continually reload them, opening new HTTP connections on each refresh:
#
#   INDEX.HTM    <meta HTTP-EQUIV="Refresh" CONTENT="10;">  (every 10 seconds)
#   SERVICES.HTM <meta HTTP-EQUIV="Refresh" CONTENT="5;">   (every 5 seconds)
#
# We replace each tag with the same number of ASCII space characters so the
# flash layout does not change.
#
# The third refresh tag in the binary (CONTENT="0; url=%s") is an HTTP
# redirect helper, not a periodic poll — we intentionally leave it untouched.

def _remove_html_refresh_tags(raw):
    """
    Find and replace periodic <meta refresh> tags with spaces in-place.

    Returns (new_raw_bytes, list_of_replacements) where each replacement is a
    (file_offset, original_tag) tuple.
    """
    raw = bytearray(raw)
    pattern = re.compile(
        rb'<meta\s+HTTP-EQUIV="Refresh"\s+CONTENT="\d+;?"[^>]*>',
        re.IGNORECASE
    )
    replacements = []
    for m in pattern.finditer(raw):
        tag   = m.group()
        start = m.start()
        raw[start : start + len(tag)] = b" " * len(tag)
        replacements.append((start, tag))
    return bytes(raw), replacements


# ──────────────────────────────────────────────────────────────────────────────
# Firmware I/O helpers
# ──────────────────────────────────────────────────────────────────────────────

def _load_firmware(path):
    """Return raw firmware bytes, unwrapping .zip if needed."""
    if path.lower().endswith(".zip"):
        with zipfile.ZipFile(path) as zf:
            names = [n for n in zf.namelist() if n.lower().endswith(".bin")]
            if not names:
                raise ValueError(f"No .bin file inside {path!r}")
            if len(names) > 1:
                # Sort so the result is deterministic; prefer names starting with MPS
                names.sort(key=lambda n: (not n.upper().startswith("MPS"), n))
                print(f"  Note: multiple .bin files in zip; using {names[0]!r}")
            return zf.read(names[0])
    with open(path, "rb") as fh:
        return fh.read()


def _patch_uimage_crc(fw, new_payload):
    """Recompute U-Boot uImage header CRCs after changing the LZMA payload."""
    hdr = bytearray(fw[UIMAGE_OFFSET : UIMAGE_OFFSET + 64])
    struct.pack_into(">I", hdr, 12, len(new_payload))              # data size
    dcrc = zlib.crc32(new_payload) & 0xFFFFFFFF
    struct.pack_into(">I", hdr, 24, dcrc)                          # data CRC
    struct.pack_into(">I", hdr,  4, 0)                             # zero hdr CRC
    hcrc = zlib.crc32(bytes(hdr)) & 0xFFFFFFFF
    struct.pack_into(">I", hdr,  4, hcrc)                          # header CRC
    return bytes(hdr)


def _recompress_and_rebuild(fw, raw_patched):
    """
    Recompress *raw_patched* with the same LZMA settings as the original, then
    rebuild and return the complete firmware binary with updated CRC fields.
    """
    filters = [{
        "id":        lzma.FILTER_LZMA1,
        "dict_size": 8 * 1024 * 1024,
        "lc": 3, "lp": 0, "pb": 2,
        "preset": lzma.PRESET_DEFAULT,
    }]
    new_lzma = lzma.compress(bytes(raw_patched), format=lzma.FORMAT_ALONE,
                              filters=filters)

    padding        = fw[UIMAGE_OFFSET + 64 : LZMA_OFFSET]
    uimage_payload = bytes(padding) + new_lzma
    new_uimage_hdr = _patch_uimage_crc(fw, uimage_payload)

    new_fw = bytearray(fw[:UIMAGE_OFFSET] + new_uimage_hdr + padding + new_lzma)

    # Update ZOT header size field (bytes 8–11, LE uint32 = bytes after ZOT header)
    struct.pack_into("<I", new_fw, 8, len(new_fw) - UIMAGE_OFFSET)

    # Update ZOT header checksum (bytes 0–3 = bitwise complement of CRC32 of fw[4:])
    zot_crc = zlib.crc32(bytes(new_fw[4:])) ^ 0xFFFFFFFF
    struct.pack_into("<I", new_fw, 0, zot_crc)

    return bytes(new_fw)


# ──────────────────────────────────────────────────────────────────────────────
# Main patch orchestrator
# ──────────────────────────────────────────────────────────────────────────────

def patch(input_path, output_path):
    print(f"Loading {input_path} …")
    fw = _load_firmware(input_path)
    print(f"  Firmware size:  {len(fw):,} bytes")

    # Extract version string from ZOT header
    try:
        ver = fw[0x28:0x5B].rstrip(b"\x00").decode("latin-1")
        print(f"  Version:        {ver}")
    except Exception:
        pass

    print("\nDecompressing LZMA payload …")
    try:
        raw = bytearray(lzma.decompress(fw[LZMA_OFFSET:]))
    except Exception as exc:
        print(f"  ERROR: could not decompress firmware: {exc}")
        print(
            "\n  This does not look like a GPSU21 firmware image.\n"
            "  Supported files:\n"
            "    MPS56_90956F_9034_20191119.zip / .bin  (2019 ZOT build 9034)\n"
            "    MPS56_IOG_GPSU21_20171123.zip / .bin   (2017 IOGear build 9032)\n"
        )
        sys.exit(1)
    print(f"  Decompressed:   {len(raw):,} bytes")

    total_patches = 0

    # ── Patch 1: mDNS_Lock counter resync ────────────────────────────────────
    print("\n[1/4] mDNS_Lock counter resync …")
    try:
        block_off, sp_field = _find_mdns_error_block(raw, _MDNS_LOCK_STR)
        patch_bytes = _build_mdns_sync_patch(sp_field)
        raw[block_off : block_off + len(patch_bytes)] = patch_bytes
        print(f"  ✓  Patched {len(patch_bytes)} bytes at "
              f"file+0x{block_off:07x} / VA 0x{MIPS_BASE+block_off:08x}")
        total_patches += 1
    except RuntimeError as exc:
        print(f"  ✗  {exc}")
        print("     Skipping mDNS_Lock patch — Bonjour may still drop.")

    # ── Patch 2: mDNS_Unlock counter resync ──────────────────────────────────
    print("\n[2/4] mDNS_Unlock counter resync …")
    try:
        block_off, sp_field = _find_mdns_error_block(raw, _MDNS_UNLOCK_STR)
        patch_bytes = _build_mdns_sync_patch(sp_field)
        raw[block_off : block_off + len(patch_bytes)] = patch_bytes
        print(f"  ✓  Patched {len(patch_bytes)} bytes at "
              f"file+0x{block_off:07x} / VA 0x{MIPS_BASE+block_off:08x}")
        total_patches += 1
    except RuntimeError as exc:
        print(f"  ✗  {exc}")
        print("     Skipping mDNS_Unlock patch.")

    # ── Patch 3: eCos wakeup_count overflow bypass ────────────────────────────
    print("\n[3/4] eCos wakeup_count overflow bypass …")
    try:
        beqz_off = _find_wakeup_overflow_beqz(raw)
        original_beqz = bytes(raw[beqz_off : beqz_off + 4])
        raw[beqz_off : beqz_off + 4] = _NOP
        print(f"  ✓  NOP'd BEQZ at "
              f"file+0x{beqz_off:07x} / VA 0x{MIPS_BASE+beqz_off:08x} "
              f"(was {original_beqz.hex()})")
        total_patches += 1
    except RuntimeError as exc:
        print(f"  ✗  {exc}")
        print("     Skipping eCos overflow patch.")

    # ── Patch 4: Remove HTML auto-refresh tags ────────────────────────────────
    print("\n[4/4] Removing HTML auto-refresh tags …")
    raw_bytes, replacements = _remove_html_refresh_tags(bytes(raw))
    raw = bytearray(raw_bytes)
    if replacements:
        for file_off, tag in replacements:
            print(f"  ✓  Removed {tag.decode('latin-1')!r}")
            print(f"     at file+0x{file_off:07x} / VA 0x{MIPS_BASE+file_off:08x}")
        total_patches += len(replacements)
    else:
        print("  ✗  No auto-refresh meta tags found — possibly already removed.")

    if total_patches == 0:
        print("\nERROR: No patches could be applied. "
              "Is this a supported GPSU21 firmware image?")
        sys.exit(1)

    # ── Recompress and rebuild ────────────────────────────────────────────────
    print(f"\nRecompressing LZMA payload ({total_patches} patches applied) …")
    new_fw = _recompress_and_rebuild(fw, raw)
    print(f"  New firmware size: {len(new_fw):,} bytes "
          f"(original: {len(fw):,})")

    with open(output_path, "wb") as fh:
        fh.write(new_fw)

    print(f"\n{'─'*60}")
    print(f"Patched firmware written → {output_path}")
    print()
    print("Flash instructions:")
    print("  1. Open http://<printer-ip>/ in your browser")
    print("  2. Go to the firmware upgrade page")
    print(f"  3. Upload  {output_path}")
    print("  4. Wait ~60 s — do NOT power off during the upgrade")
    print()
    print("Patches applied:")
    print("  • mDNS_Lock + mDNS_Unlock counter resync  → Bonjour stays working")
    print("  • eCos wakeup_count overflow bypass       → scheduler no longer freezes")
    print("  • HTML auto-refresh removal               → HTTP pool exhaustion reduced")


# ──────────────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    input_path, output_path = sys.argv[1], sys.argv[2]
    if not output_path.lower().endswith(".bin"):
        print("ERROR: output file must end in .bin")
        sys.exit(1)
    patch(input_path, output_path)
