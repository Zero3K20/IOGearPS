#!/usr/bin/env bash
# Patch: io/eth lwIP + standalone driver coexistence
#
# Problem
# -------
# eCos 3.0 packages/io/eth compiles stand_alone/eth_drv.c even when a full
# network stack (CYGPKG_NET_LWIP) is configured.  When BOTH
# CYGPKG_IO_ETH_DRIVERS_STAND_ALONE and CYGPKG_NET_LWIP are active:
#
#   1. eth_drv.h defines `struct arpcom` twice — once for standalone use
#      (with .esa member) and once for lwIP (without .esa), producing a
#      "redefinition of struct arpcom" error.
#
#   2. stand_alone/eth_drv.c references the .esa member which does not exist
#      in the lwIP version of struct arpcom.
#
# Fix 1 — eth_drv.h
# ------------------
# Guard the STAND_ALONE struct arpcom definition so it is skipped when lwIP
# is present.
#
#   Before:  #ifdef CYGPKG_IO_ETH_DRIVERS_STAND_ALONE
#   After:   #if defined(CYGPKG_IO_ETH_DRIVERS_STAND_ALONE) && !defined(CYGPKG_NET_LWIP)
#
# Fix 2 — stand_alone/eth_drv.c
# -------------------------------
# Wrap the entire translation unit in a !CYGPKG_NET_LWIP guard so the file
# compiles to an empty object when lwIP is configured.
#
# Usage
# -----
#   bash ecos/patches/0001-io-eth-lwip-compat.sh <ecos-src-root>
#
#   <ecos-src-root>  Path to the top-level eCos source directory
#                    (the one that contains a "packages/" subdirectory).
#
# Idempotency
# -----------
# Both transformations are idempotent: on subsequent runs the patterns no
# longer match in the already-patched file so sed/grep makes no change.

set -euo pipefail

ECOS_ROOT="${1:?Usage: $0 <ecos-src-root>}"

ETH_HDR="${ECOS_ROOT}/packages/io/eth/v3_0/include/eth_drv.h"
ETH_SA="${ECOS_ROOT}/packages/io/eth/v3_0/src/stand_alone/eth_drv.c"

# ── Validate paths ────────────────────────────────────────────────────────────
for f in "$ETH_HDR" "$ETH_SA"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: expected file not found: $f" >&2
        exit 1
    fi
done

# ── Fix 1: eth_drv.h — guard STAND_ALONE struct arpcom ───────────────────────
sed -i \
    's/^#ifdef CYGPKG_IO_ETH_DRIVERS_STAND_ALONE$/#if defined(CYGPKG_IO_ETH_DRIVERS_STAND_ALONE) \&\& !defined(CYGPKG_NET_LWIP)/' \
    "$ETH_HDR"
echo "Patched: $ETH_HDR"

# ── Fix 2: stand_alone/eth_drv.c — wrap in !CYGPKG_NET_LWIP guard ─────────────
if ! grep -q 'CYGPKG_NET_LWIP' "$ETH_SA"; then
    FIRST_PP=$(grep -n '^#' "$ETH_SA" | head -1 | cut -d: -f1)
    if [ -z "$FIRST_PP" ]; then
        echo "ERROR: no preprocessor directive found in $ETH_SA" >&2
        exit 1
    fi
    TMP=$(mktemp)
    awk -v pp="$FIRST_PP" \
        'NR==pp{print "#include <pkgconf/system.h>"; print "#ifndef CYGPKG_NET_LWIP"}1' \
        "$ETH_SA" > "$TMP" && mv "$TMP" "$ETH_SA"
    printf '\n#endif /* !CYGPKG_NET_LWIP */\n' >> "$ETH_SA"
    echo "Patched: $ETH_SA"
else
    echo "Already patched (skipping): $ETH_SA"
fi
