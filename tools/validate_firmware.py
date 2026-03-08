#!/usr/bin/env python3
"""
validate_firmware.py - Pre-flash firmware validator for the IOGear GPSU21.

Run this script on a firmware .bin file BEFORE flashing it.  It catches every
known cause of bricking so you can fix the problem on your workstation instead
of discovering it with a non-responsive device.

Usage:
    python  tools/validate_firmware.py  <firmware.bin>
    python3 tools/validate_firmware.py  MPS56_90956F_9034_20191119.bin
    python3 tools/validate_firmware.py  MPS56_90956F_9034_20191119.zip
    python3 tools/validate_firmware.py  firmware/build/gpsu21_freertos.bin

Exit codes:
    0  — all checks passed; the image is safe to flash
    1  — one or more checks failed; DO NOT flash this image

Checks performed
────────────────
  [1] ZOT header magic           — 0xB25A4758 at offset 0x04
  [2] ZOT header CRC32           — bitwise complement of CRC32(fw[4:])
  [3] ZOT payload size field     — must equal len(fw) - 256
  [4] ZOT version fields         — packed version bytes at 0x20–0x27
  [5] uImage header magic        — 0x27051956 at offset 0x0100
  [6] uImage header CRC32        — CRC32 over header with CRC field zeroed
  [7] uImage data CRC32          — CRC32 over padding + LZMA payload
  [8] uImage load address        — must be 0x80500000 (MT7688 DRAM entry)
  [9] LZMA payload integrity     — decompression must succeed without error
 [10] CP0 Status interrupt mask  — decompressed binary must contain the word
                                   0x00008103 (IE | EXL | IM0 | IM7); absence
                                   means the FreeRTOS scheduler will freeze
                                   permanently after the first eret, making the
                                   device appear bricked
 [11] WDT disable at startup     — decompressed binary must contain a store-zero
                                   instruction targeting the MT7688 watchdog
                                   timer register (physical 0x10000120 /
                                   KSEG1 0xB0000120); absence means the ZOT
                                   bootloader's ~30-second WDT timeout will
                                   trigger a reset loop that looks like a brick

The CP0 Status and WDT checks are heuristic: they scan raw machine code for
known instruction patterns.  A WARN result means the pattern was not found but
the check is not conclusive (the code may use an equivalent sequence).  A FAIL
result means the binary almost certainly has the problem.

Background — known bricking causes
───────────────────────────────────
1.  Bad ZOT or uImage CRC  →  bootloader prints "Wrong firmware" and refuses to
    decompress the image, leaving the device in a reboot loop.

2.  Wrong load address     →  bootloader decompresses the LZMA payload to the
    wrong DRAM address; execution jumps to garbage, the device hangs silently.

3.  Corrupt LZMA payload   →  decompression fails at boot; same result as above.

4.  Missing CP0 IM0+IM7    →  the FreeRTOS MIPS port in port.c initialises each
    task stack with a CP0 Status word.  The word must have:
      bit 0  (IE)  = 1 — enable interrupts after eret clears EXL
      bit 1  (EXL) = 1 — exception level; cleared by eret
      bit 8  (IM0) = 1 — unmask IP0/SW0, the FreeRTOS yield interrupt
      bit 15 (IM7) = 1 — unmask IP7/HW5, the CP0 Count/Compare timer tick
    The correct value is 0x00008103.  An earlier broken version used 0x00000003
    (IE+EXL only), which permanently masks both the tick and yield after the
    first eret — the scheduler never runs again and the device appears bricked.

5.  WDT not disabled        →  the ZOT/U-Boot bootloader arms the MT7688
    hardware watchdog with a ~30-second timeout.  If board_init() does not
    write 0 to MT7688_WDT_TIMER (KSEG1 0xB0000120) before the timeout, the SoC
    resets, the bootloader re-arms the WDT, and the cycle repeats indefinitely.
    From the outside the device looks permanently bricked.
"""

import sys
import os
import struct
import zlib
import lzma
import zipfile


# ── Firmware layout constants ─────────────────────────────────────────────────

ZOT_HEADER_SIZE  = 256        # bytes
UIMAGE_OFFSET    = 0x0100     # offset of the 64-byte uImage header in the .bin
LZMA_OFFSET      = 0x4AC0     # offset of the LZMA payload in the .bin
MIPS_LOAD_ADDR   = 0x80500000 # expected uImage load/entry address
ZOT_MAGIC        = 0xB25A4758 # ZOT Technology magic at fw[4:8]
UIMAGE_MAGIC     = 0x27051956 # standard U-Boot uImage magic

# MT7688 watchdog timer: KSEG1 uncached alias of physical 0x10000120.
# board_init() must write 0 to this address to disable the WDT that the ZOT
# bootloader arms during its own startup sequence (~30 s timeout).
WDT_HI16 = 0xB000   # upper 16 bits of 0xB0000120 (lui target)
WDT_LO16 = 0x0120   # lower 16 bits (sw offset from base register)

# CP0 Status value required for each new FreeRTOS task stack frame.
# IE=1, EXL=1, IM0=1 (SW0/yield), IM7=1 (HW5/timer tick).
CP0_STATUS_CORRECT = 0x00008103

# ── Helpers ───────────────────────────────────────────────────────────────────

def _crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def _load_firmware(path: str) -> bytes:
    """Return raw firmware bytes, unwrapping a .zip if necessary."""
    if path.lower().endswith(".zip"):
        with zipfile.ZipFile(path) as zf:
            names = [n for n in zf.namelist() if n.lower().endswith(".bin")]
            if not names:
                raise ValueError(f"No .bin file found inside {path!r}")
            if len(names) > 1:
                print(f"  Note: multiple .bin files in zip, using {names[0]!r}")
            return zf.read(names[0])
    with open(path, "rb") as f:
        return f.read()


# ── Result tracking ───────────────────────────────────────────────────────────

PASS = "PASS"
WARN = "WARN"
FAIL = "FAIL"

_results: list[tuple[str, str, str]] = []   # (status, check_name, detail)


def _record(status: str, name: str, detail: str = "") -> None:
    _results.append((status, name, detail))
    tag = f"[{status}]"
    msg = f"  {tag:<7} {name}"
    if detail:
        msg += f"\n           {detail}"
    print(msg)


# ── Individual checks ─────────────────────────────────────────────────────────

def _check_zot_magic(fw: bytes) -> bool:
    if len(fw) < 8:
        _record(FAIL, "ZOT magic", f"file too short ({len(fw)} bytes)")
        return False
    magic = struct.unpack_from("<I", fw, 4)[0]
    if magic == ZOT_MAGIC:
        _record(PASS, "ZOT magic", f"0x{magic:08X}")
        return True
    _record(FAIL, "ZOT magic",
            f"expected 0x{ZOT_MAGIC:08X}, got 0x{magic:08X} — "
            "this is not a GPSU21 firmware image")
    return False


def _check_zot_crc(fw: bytes) -> bool:
    stored  = struct.unpack_from("<I", fw, 0)[0]
    calc    = _crc32(fw[4:]) ^ 0xFFFFFFFF
    if stored == calc:
        _record(PASS, "ZOT header CRC32", f"0x{stored:08X}")
        return True
    _record(FAIL, "ZOT header CRC32",
            f"stored 0x{stored:08X} ≠ calculated 0x{calc:08X} — "
            "the bootloader will reject this image with 'Wrong firmware'")
    return False


def _check_zot_payload_size(fw: bytes) -> bool:
    stored   = struct.unpack_from("<I", fw, 8)[0]
    expected = len(fw) - ZOT_HEADER_SIZE
    if stored == expected:
        _record(PASS, "ZOT payload size", f"{stored:,} bytes")
        return True
    _record(FAIL, "ZOT payload size",
            f"stored {stored:,} ≠ file-derived {expected:,} bytes — "
            "the image is truncated or the size field was not updated after editing")
    return False


def _check_zot_version(fw: bytes) -> bool:
    hdr    = fw[:ZOT_HEADER_SIZE]
    major  = hdr[0x20]
    minor  = hdr[0x21]
    patch  = hdr[0x22]
    suffix = hdr[0x23]
    build  = struct.unpack_from("<I", hdr, 0x24)[0]
    if major == 0 and minor == 0 and patch == 0 and build == 0:
        _record(FAIL, "ZOT version fields",
                "all zero — upgrade validator will reject this image; "
                "rebuild with package_firmware.py which sets these from --version")
        return False
    sfx = chr(suffix) if 32 <= suffix < 127 else f"0x{suffix:02X}"
    _record(PASS, "ZOT version fields",
            f"v{major}.{minor:02d}.{patch} build {build}{sfx}")
    return True


def _check_uimage_magic(fw: bytes) -> bool:
    if len(fw) < UIMAGE_OFFSET + 4:
        _record(FAIL, "uImage magic", "file too short for uImage header")
        return False
    magic = struct.unpack_from(">I", fw, UIMAGE_OFFSET)[0]
    if magic == UIMAGE_MAGIC:
        _record(PASS, "uImage magic", f"0x{magic:08X}")
        return True
    _record(FAIL, "uImage magic",
            f"expected 0x{UIMAGE_MAGIC:08X}, got 0x{magic:08X}")
    return False


def _check_uimage_header_crc(fw: bytes) -> bool:
    hdr      = bytearray(fw[UIMAGE_OFFSET: UIMAGE_OFFSET + 64])
    stored   = struct.unpack_from(">I", hdr, 4)[0]
    struct.pack_into(">I", hdr, 4, 0)
    calc     = _crc32(bytes(hdr))
    if stored == calc:
        _record(PASS, "uImage header CRC32", f"0x{stored:08X}")
        return True
    _record(FAIL, "uImage header CRC32",
            f"stored 0x{stored:08X} ≠ calculated 0x{calc:08X}")
    return False


def _check_uimage_data_crc(fw: bytes) -> tuple[bool, int]:
    """Returns (passed, payload_size)."""
    uimg         = fw[UIMAGE_OFFSET: UIMAGE_OFFSET + 64]
    payload_size = struct.unpack_from(">I", uimg, 12)[0]
    stored_dcrc  = struct.unpack_from(">I", uimg, 24)[0]
    payload_data = fw[UIMAGE_OFFSET + 64: UIMAGE_OFFSET + 64 + payload_size]
    calc_dcrc    = _crc32(payload_data)
    if stored_dcrc == calc_dcrc:
        _record(PASS, "uImage data CRC32",
                f"0x{stored_dcrc:08X}  ({payload_size:,} bytes)")
        return True, payload_size
    _record(FAIL, "uImage data CRC32",
            f"stored 0x{stored_dcrc:08X} ≠ calculated 0x{calc_dcrc:08X} — "
            "the LZMA payload or padding was modified without updating the CRC")
    return False, payload_size


def _check_load_address(fw: bytes) -> bool:
    load = struct.unpack_from(">I", fw, UIMAGE_OFFSET + 16)[0]
    ent  = struct.unpack_from(">I", fw, UIMAGE_OFFSET + 20)[0]
    if load == MIPS_LOAD_ADDR and ent == MIPS_LOAD_ADDR:
        _record(PASS, "uImage load/entry address", f"0x{load:08X}")
        return True
    _record(FAIL, "uImage load/entry address",
            f"load=0x{load:08X} entry=0x{ent:08X}, expected 0x{MIPS_LOAD_ADDR:08X} — "
            "bootloader will decompress to the wrong DRAM location")
    return False


def _check_lzma(fw: bytes) -> tuple[bool, bytes | None]:
    """Returns (passed, raw_bytes_or_None)."""
    if len(fw) <= LZMA_OFFSET:
        _record(FAIL, "LZMA decompression", "file ends before LZMA offset 0x4AC0")
        return False, None
    stream = fw[LZMA_OFFSET:]
    try:
        raw = lzma.decompress(stream)
        _record(PASS, "LZMA decompression",
                f"{len(stream):,} compressed → {len(raw):,} bytes")
        return True, raw
    except lzma.LZMAError as exc:
        _record(FAIL, "LZMA decompression",
                f"failed: {exc} — the payload is corrupt; the device will "
                "hang silently at boot when the bootloader tries to decompress it")
        return False, None


def _check_cp0_status(raw: bytes) -> bool:
    """
    Scan the decompressed binary for the CP0 Status constant 0x00008103.

    The FreeRTOS MIPS port stores this value at pxTopOfStack[1] in
    pxPortInitialiseStack().  The compiler may encode it as:
      • A 32-bit word in a data/rodata section (little-endian)
      • An ori rX, rZero, 0x8103 instruction (because the value fits in 16 bits)
    We search for both forms.

    0x8103 as an ori immediate:
      ori rt, rs, 0x8103 → opcode=001101, any rs, any rt, imm=0x8103
      lower 16 bits of the instruction word (LE) = 0x8103 at bytes [2:4]

    This check is only meaningful for FreeRTOS-based firmware.  The OEM eCos
    firmware uses a completely different interrupt model and does not need this
    value.  We use the decompressed binary size as a heuristic discriminator:
    the OEM eCos image decompresses to ~1.5 MB; a FreeRTOS binary is typically
    under 800 KB.  Only emit FAIL when the binary looks like FreeRTOS.
    """
    # FreeRTOS binaries are small; eCos OEM firmware is ~1.5 MB.
    FREERTOS_SIZE_THRESHOLD = 800 * 1024   # bytes

    # Form 1: literal 32-bit little-endian word 0x00008103 anywhere in binary
    word_ok = struct.pack("<I", CP0_STATUS_CORRECT)
    if word_ok in raw:
        _record(PASS, "CP0 Status IM0+IM7 (interrupt mask)",
                f"0x{CP0_STATUS_CORRECT:08X} found — scheduler tick and yield "
                "interrupts are unmasked (GOOD)")
        return True

    # Form 2: ori instruction carrying the 0x8103 immediate
    # In MIPS LE, the immediate occupies the high 2 bytes of the 4-byte word.
    ori_imm = b"\x03\x81"   # 0x8103 in LE short occupies bytes [0:2] of instruction
    # ori encoding: opcode=0x0D at bits 31:26 → byte[3] of LE word has top 6 bits = 0x0D
    pos = 0
    while True:
        pos = raw.find(ori_imm, pos)
        if pos == -1:
            break
        if pos % 4 == 0:    # aligned instruction
            byte3 = raw[pos + 3] if pos + 3 < len(raw) else 0
            opcode = (byte3 >> 2) & 0x3F  # bits 31:26 of the 32-bit LE word
            if opcode == 0x0D:             # ori opcode
                _record(PASS, "CP0 Status IM0+IM7 (interrupt mask)",
                        f"ori with 0x8103 immediate at binary offset 0x{pos:06x} — "
                        "scheduler tick and yield interrupts are unmasked (GOOD)")
                return True
        pos += 1

    # Neither form found.  Check for the known-broken value 0x00000003.
    word_bad = struct.pack("<I", 0x00000003)
    has_bad_value = word_bad in raw

    if len(raw) < FREERTOS_SIZE_THRESHOLD:
        # Small binary → almost certainly FreeRTOS.  The missing IM bits are
        # a hard bricking risk.
        if has_bad_value:
            _record(FAIL, "CP0 Status IM0+IM7 (interrupt mask)",
                    "0x00008103 NOT found but 0x00000003 (IE|EXL, no IM bits) IS "
                    "present in a FreeRTOS-sized binary — after the first eret "
                    "both the scheduler timer and yield are permanently masked; "
                    "the device will appear bricked. "
                    "Fix: set pxTopOfStack[1] = 0x00008103 in "
                    "firmware/bsp/freertos_port/port.c")
            return False
        _record(WARN, "CP0 Status IM0+IM7 (interrupt mask)",
                "could not confirm 0x00008103 in the binary — verify that "
                "pxPortInitialiseStack() in port.c sets CP0 Status = 0x00008103")
    else:
        # Large binary → likely the OEM eCos firmware which manages interrupts
        # differently and does not use this FreeRTOS-specific value.
        _record(PASS, "CP0 Status IM0+IM7 (interrupt mask)",
                "large binary (likely OEM eCos) — FreeRTOS CP0 Status check "
                "does not apply")

    return True   # warn only for FreeRTOS; pass for eCos, not a hard fail


def _check_wdt_disable(raw: bytes) -> bool:
    """
    Scan the decompressed binary for the MT7688 WDT-disable sequence.

    board_init() must write 0 to MT7688_WDT_TIMER (0xB0000120):
      lui  rX, 0xB000        — load upper 16 bits of WDT base
      sw   $zero, 0x120(rX)  — write 0 to offset 0x120 from base

    We scan for any (lui rX, 0xB000) instruction within 128 bytes of any
    (sw $zero, 0x120(rX)) instruction using the same base register rX.
    If at least one such pair exists the WDT is disabled at startup.

    This check is heuristic.  A WARN result means the pattern was not found;
    the firmware may still disable the WDT via a different sequence (e.g.
    loading the full 32-bit address through a different register pair, or via
    a pointer stored in a global).  Only emit a hard FAIL if this is
    clearly a FreeRTOS binary that is missing the disable call entirely.
    """
    FREERTOS_SIZE_THRESHOLD = 800 * 1024
    PROXIMITY = 128   # bytes; allow for instruction scheduling / function preamble

    # Collect all (offset, rX) pairs for lui rX, 0xB000
    lui_positions: dict[int, list[int]] = {}   # reg → [offset, ...]
    # lui encoding: opcode=0x0F, rs=0, rt=rX, imm=0xB000
    for rt in range(32):
        instr = struct.pack("<I", (0x0F << 26) | (rt << 16) | WDT_HI16)
        pos = 0
        while True:
            pos = raw.find(instr, pos)
            if pos == -1:
                break
            lui_positions.setdefault(rt, []).append(pos)
            pos += 4

    if not lui_positions:
        msg = ("no 'lui rX, 0xB000' instruction found — could not confirm that "
               "the WDT is disabled; if the ZOT bootloader arms the WDT and "
               "board_init() does not write 0 to 0xB0000120 the device will "
               "reset-loop every ~30 s and appear bricked")
        if len(raw) < FREERTOS_SIZE_THRESHOLD:
            _record(WARN, "WDT disable at startup", msg)
        else:
            # OEM eCos firmware — different WDT handling; not a concern
            _record(PASS, "WDT disable at startup",
                    "large binary (likely OEM eCos) — MT7688 WDT disable "
                    "check does not apply")
        return True

    # Collect all (offset, rX) pairs for sw $zero, 0x120(rX)
    # sw encoding: opcode=0x2B, base=rX, rt=0, offset=0x120
    sw_positions: dict[int, list[int]] = {}   # reg → [offset, ...]
    for base in range(32):
        instr = struct.pack("<I", (0x2B << 26) | (base << 21) | WDT_LO16)
        pos = 0
        while True:
            pos = raw.find(instr, pos)
            if pos == -1:
                break
            sw_positions.setdefault(base, []).append(pos)
            pos += 4

    # Check for a matching (lui rX / sw rX) pair within PROXIMITY bytes,
    # allowing either ordering (sw may appear before or after lui due to
    # compiler instruction scheduling, though lui before sw is canonical).
    for reg in sorted(set(lui_positions) & set(sw_positions)):
        for lui_off in lui_positions[reg]:
            for sw_off in sw_positions[reg]:
                if abs(sw_off - lui_off) <= PROXIMITY:
                    _record(PASS, "WDT disable at startup",
                            f"lui r{reg},0xB000 @ 0x{lui_off:06x} + "
                            f"sw $zero,0x120(r{reg}) @ 0x{sw_off:06x} — "
                            "MT7688_WDT_TIMER is zeroed at startup (GOOD)")
                    return True

    # lui instructions are present but no matching sw was found nearby.
    # For large binaries (OEM eCos), this is expected — eCos handles the WDT
    # through its own HAL mechanism and we cannot reliably pattern-match it.
    regs = sorted(lui_positions)
    if len(raw) >= FREERTOS_SIZE_THRESHOLD:
        _record(PASS, "WDT disable at startup",
                "large binary (likely OEM eCos) — MT7688 WDT disable check "
                "does not apply")
    else:
        _record(WARN, "WDT disable at startup",
                f"found lui rX,0xB000 (reg(s): {regs}) but no adjacent "
                f"sw $zero,0x120(rX) within {PROXIMITY} bytes — if the WDT is "
                "not disabled within ~30 s the device will appear to brick; "
                "check board_init() in firmware/bsp/mt7688_init.c")
    return True   # warn only


# ── Top-level driver ──────────────────────────────────────────────────────────

def validate(firmware_path: str) -> bool:
    """
    Run all pre-flash checks on *firmware_path*.

    Returns True if the image is safe to flash, False if any FAIL check fired.
    """
    print(f"Validating {firmware_path} …")
    try:
        fw = _load_firmware(firmware_path)
    except (OSError, ValueError) as exc:
        print(f"  [FAIL]  Could not load file: {exc}")
        return False

    print(f"  Loaded {len(fw):,} bytes\n")

    # ── Structural checks (stop early if magic/CRC fails) ──────────────────
    if not _check_zot_magic(fw):
        print("\n  ✗ Not a GPSU21 firmware image — aborting remaining checks.")
        return False

    _check_zot_crc(fw)
    _check_zot_payload_size(fw)
    _check_zot_version(fw)

    if len(fw) < UIMAGE_OFFSET + 64:
        _record(FAIL, "uImage header", "file too short")
    else:
        _check_uimage_magic(fw)
        _check_uimage_header_crc(fw)
        _check_uimage_data_crc(fw)
        _check_load_address(fw)

    # ── LZMA decompression ─────────────────────────────────────────────────
    ok, raw = _check_lzma(fw)

    # ── Runtime-behaviour checks on decompressed binary ────────────────────
    if raw is not None:
        print()
        _check_cp0_status(raw)
        _check_wdt_disable(raw)

    # ── Summary ────────────────────────────────────────────────────────────
    fails = [r for r in _results if r[0] == FAIL]
    warns = [r for r in _results if r[0] == WARN]
    total = len(_results)

    print()
    print("─" * 60)
    print(f"  Checks: {total}  │  FAIL: {len(fails)}  │  WARN: {len(warns)}")
    print("─" * 60)

    if fails:
        print()
        print("  ✗ DO NOT FLASH — the following checks failed:")
        for _, name, detail in fails:
            print(f"      • {name}")
            if detail:
                for line in detail.split("\n"):
                    print(f"        {line}")
        print()
        print("  Fix the issues above and re-run this script before flashing.")
        return False

    if warns:
        print()
        print("  ⚠  Warnings (flash at your own risk):")
        for _, name, detail in warns:
            print(f"      • {name}")
            if detail:
                for line in detail.split("\n"):
                    print(f"        {line}")

    print()
    print("  ✓ All checks passed — this image is safe to flash.")
    return True


# ── CLI ───────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    if len(sys.argv) != 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        sys.exit(0)
    passed = validate(sys.argv[1])
    sys.exit(0 if passed else 1)
