#!/usr/bin/env bash
# Patch: suppress -Wexpansion-to-defined warnings in lwIP opt.h
#
# Problem
# -------
# eCos 3.0 generates CDL config headers where boolean lwIP options such as
# LWIP_STATS and PPP_SUPPORT are defined as macros whose expansion contains
# the "defined" operator.  For example:
#
#   #define LWIP_STATS defined(CYGOPT_NET_LWIP_STATS)
#
# When opt.h later evaluates these macros in a #if directive:
#
#   #if LWIP_STATS           /* line 362 */
#   #if PPP_SUPPORT          /* line 435 */
#
# GCC expands the macro and sees "defined(...)" in the result, triggering:
#
#   warning: this use of "defined" may not be portable [-Wexpansion-to-defined]
#
# This is a long-standing GCC warning (added in GCC 6) that became more
# prominent with -Wall/-Wextra.  The underlying behaviour is correct; the
# macros evaluate as intended.
#
# Fix
# ---
# Wrap each problematic #if line in opt.h with a narrow
# #pragma GCC diagnostic push/ignored/pop block to suppress the
# -Wexpansion-to-defined warning for exactly those two lines.  This is
# safe because:
#   - The pragma is self-contained within opt.h (push balanced by pop).
#   - It only suppresses the specific portability warning, not errors.
#   - The lwIP CDL-generated macro values are correct for eCos even if
#     they use the "defined" idiom.
#
# Usage
# -----
#   bash ecos/patches/0004-lwip-opt-expansion-to-defined.sh <ecos-src-root>
#
# Idempotency
# -----------
# A sentinel string is checked before any change is made; re-running the
# script against an already-patched file is a no-op.

set -euo pipefail

ECOS_ROOT="${1:?Usage: $0 <ecos-src-root>}"

OPT_H="${ECOS_ROOT}/packages/net/lwip_tcpip/v3_0/include/lwip/opt.h"

if [ ! -f "$OPT_H" ]; then
    echo "ERROR: expected file not found: $OPT_H" >&2
    exit 1
fi

python3 - "$OPT_H" <<'PYEOF'
import sys, re

path = sys.argv[1]

with open(path, 'r') as f:
    content = f.read()

sentinel = 'opt.h expansion-to-defined'
if sentinel in content:
    print(f"Already patched (skipping): {path}")
    sys.exit(0)

# Add push/pop pragmas around each problematic #if line.
# We insert:
#   #pragma GCC diagnostic push
#   #pragma GCC diagnostic ignored "-Wexpansion-to-defined"
# immediately before the #if line, and:
#   #pragma GCC diagnostic pop
# immediately after it.
#
# This keeps the suppression as narrow as possible.
push = ('#pragma GCC diagnostic push /* opt.h expansion-to-defined */\n'
        '#pragma GCC diagnostic ignored "-Wexpansion-to-defined"\n')
pop  = '#pragma GCC diagnostic pop /* opt.h expansion-to-defined */\n'

def wrap_if(m):
    return push + m.group(0) + '\n' + pop

# Match "#if LWIP_STATS" and "#if PPP_SUPPORT" lines (and only those lines).
new = re.sub(r'^(#if (?:LWIP_STATS|PPP_SUPPORT)\b[^\n]*)$',
             wrap_if,
             content,
             flags=re.MULTILINE)

if new != content:
    with open(path, 'w') as f:
        f.write(new)
    print(f"Patched: {path}")
else:
    print(f"No matching #if lines found (skipping): {path}")
PYEOF

echo "eCos lwip/opt.h expansion-to-defined patch done."
