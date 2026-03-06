#!/usr/bin/env bash
# Patch: suppress GCC warnings in lwIP etharp.h, ip.h, and etharp.c
#
# Problems
# --------
# 1. netif/etharp.h and lwip/ip.h — "packed attribute ignored for field of
#    type 'struct ...'" [-Wattributes]
#
#    lwIP uses the PACK_STRUCT_FIELD() macro to mark individual struct members
#    as packed.  GCC (since ~4.8) silently ignores the packed attribute when
#    applied to a field whose type is itself a struct — it only honours the
#    attribute on scalar or pointer fields.  With -Wall/-Wextra the compiler
#    emits one [-Wattributes] diagnostic per affected field.
#
#    Fix: wrap the struct definitions in etharp.h and ip.h with
#         #pragma GCC diagnostic push/ignored/pop to suppress the noise.
#
# 2. net/lwip_tcpip/v3_0/src/netif/etharp.c — "dereferencing type-punned
#    pointer will break strict-aliasing rules" [-Wstrict-aliasing]
#
#    etharp.c contains patterns like *(ip_addr_t*)&some_ptr that violate
#    C99 strict-aliasing rules.  These are lwIP internal implementation details
#    that are hard to refactor without risk; suppressing the warning is safe.
#
#    Fix: wrap the problematic function bodies with push/ignored/pop pragmas.
#
# Usage
# -----
#   bash ecos/patches/0003-lwip-etharp-warnings.sh <ecos-src-root>
#
# Idempotency
# -----------
# Each transformation checks whether the guard sentinel is already present
# before making any change.

set -euo pipefail

ECOS_ROOT="${1:?Usage: $0 <ecos-src-root>}"

ETHARP_H="${ECOS_ROOT}/packages/net/lwip_tcpip/v3_0/include/netif/etharp.h"
IP_H="${ECOS_ROOT}/packages/net/lwip_tcpip/v3_0/include/lwip/ip.h"
ETHARP_C="${ECOS_ROOT}/packages/net/lwip_tcpip/v3_0/src/netif/etharp.c"

for f in "$ETHARP_H" "$IP_H" "$ETHARP_C"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: expected file not found: $f" >&2
        exit 1
    fi
done

# ── Helper: wrap a pattern match with GCC diagnostic pragmas ─────────────────
# Usage: add_pragma_around_pattern FILE SENTINEL BEFORE_LINE AFTER_LINE
#   SENTINEL — a string whose presence means the patch is already applied
#   BEFORE    — regex for the line *before* the block to suppress
#   AFTER     — regex for the line *after* the block to suppress
add_pragma_around_pattern() {
    local file="$1"
    local sentinel="$2"
    local before_pat="$3"
    local after_pat="$4"
    local diag="$5"

    if grep -q "$sentinel" "$file"; then
        echo "Already patched (skipping pragma for '$sentinel'): $file"
        return
    fi

    python3 - "$file" "$before_pat" "$after_pat" "$diag" "$sentinel" <<'PYEOF'
import re, sys

path       = sys.argv[1]
before_pat = sys.argv[2]
after_pat  = sys.argv[3]
diag       = sys.argv[4]
sentinel   = sys.argv[5]

with open(path, 'r') as f:
    content = f.read()

# Insert push before the first matching line, and pop after the last.
before_re = re.compile(before_pat, re.MULTILINE)
after_re  = re.compile(after_pat,  re.MULTILINE)

m_before = before_re.search(content)
m_after  = after_re.search(content,  m_before.end() if m_before else 0)

if not m_before or not m_after:
    print(f"Pattern not found in {path} — skipping (before={before_pat!r}, after={after_pat!r})")
    sys.exit(0)

# Find end of the 'after' match line
after_line_end = content.find('\n', m_after.end())
if after_line_end == -1:
    after_line_end = len(content)

push_pragma = f'\n#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "{diag}"\n'
pop_pragma  = f'\n#pragma GCC diagnostic pop /* {sentinel} */\n'

result = (content[:m_before.start()] +
          push_pragma +
          content[m_before.start():after_line_end + 1] +
          pop_pragma +
          content[after_line_end + 1:])

with open(path, 'w') as f:
    f.write(result)
print(f"Patched: {path}")
PYEOF
}

# ── Fix 1a: etharp.h — packed attribute warnings ─────────────────────────────
# The two packed structs that trigger the warning are etharp_hdr and
# etharp_q_entry.  Both use PACK_STRUCT_FIELD() with struct-typed members.
if grep -q 'pragma GCC diagnostic.*etharp_hdr' "$ETHARP_H"; then
    echo "Already patched (skipping): $ETHARP_H"
else
    python3 - "$ETHARP_H" <<'PYEOF'
import re, sys

path = sys.argv[1]

with open(path, 'r') as f:
    content = f.read()

sentinel = 'pragma GCC diagnostic.*etharp_hdr'
if re.search(sentinel, content):
    print(f"Already patched (skipping): {path}")
    sys.exit(0)

# Wrap every PACK_STRUCT_BEGIN / PACK_STRUCT_END block that contains
# PACK_STRUCT_FIELD with a struct-type argument.
push = ('\n#pragma GCC diagnostic push\n'
        '#pragma GCC diagnostic ignored "-Wattributes"\n')
pop  = '\n#pragma GCC diagnostic pop /* etharp_hdr -Wattributes */\n'

# Find the first PACK_STRUCT_BEGIN and the matching PACK_STRUCT_END that
# encloses a PACK_STRUCT_FIELD(struct ...) reference.
new = re.sub(
    r'(PACK_STRUCT_BEGIN\s*\nstruct\s+(?:etharp_hdr|etharp_q_entry)\b)',
    r'\n#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wattributes"\n\1',
    content
)
new = re.sub(
    r'(} PACK_STRUCT_STRUCT;\s*\nPACK_STRUCT_END\s*\n)',
    r'\1\n#pragma GCC diagnostic pop /* etharp_hdr -Wattributes */\n',
    new,
    count=2  # two structs
)

if new != content:
    with open(path, 'w') as f:
        f.write(new)
    print(f"Patched: {path}")
else:
    print(f"No matching pattern found (skipping): {path}")
PYEOF
fi

# ── Fix 1b: ip.h — packed attribute warnings ─────────────────────────────────
if grep -q 'pragma GCC diagnostic.*ip_hdr' "$IP_H"; then
    echo "Already patched (skipping): $IP_H"
else
    python3 - "$IP_H" <<'PYEOF'
import re, sys

path = sys.argv[1]

with open(path, 'r') as f:
    content = f.read()

sentinel = 'pragma GCC diagnostic.*ip_hdr'
if re.search(sentinel, content):
    print(f"Already patched (skipping): {path}")
    sys.exit(0)

# Wrap the ip_hdr struct definition
new = re.sub(
    r'(PACK_STRUCT_BEGIN\s*\nstruct\s+ip_hdr\b)',
    r'\n#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wattributes"\n\1',
    content
)
new = re.sub(
    r'(} PACK_STRUCT_STRUCT;\s*\nPACK_STRUCT_END\s*)',
    r'\1\n#pragma GCC diagnostic pop /* ip_hdr -Wattributes */\n',
    new,
    count=1
)

if new != content:
    with open(path, 'w') as f:
        f.write(new)
    print(f"Patched: {path}")
else:
    print(f"No matching pattern found (skipping): {path}")
PYEOF
fi

# ── Fix 2: etharp.c — strict-aliasing warnings ───────────────────────────────
# Add a file-level pragma to suppress the strict-aliasing warnings that stem
# from lwIP's internal type-punned pointer usage in etharp.c.
if grep -q 'pragma GCC diagnostic.*strict-aliasing.*etharp' "$ETHARP_C"; then
    echo "Already patched (skipping): $ETHARP_C"
else
    python3 - "$ETHARP_C" <<'PYEOF'
import sys

path = sys.argv[1]

with open(path, 'r') as f:
    content = f.read()

sentinel = 'pragma GCC diagnostic.*strict-aliasing.*etharp'
import re
if re.search(sentinel, content):
    print(f"Already patched (skipping): {path}")
    sys.exit(0)

# Insert the diagnostic pragma after the initial comment block / before the
# first non-comment line.
pragma = ('#pragma GCC diagnostic ignored "-Wstrict-aliasing"'
          ' /* etharp.c internal type punning */\n')

# Find the first line that is not a comment and not blank
lines = content.split('\n')
insert_at = 0
for i, line in enumerate(lines):
    stripped = line.strip()
    if stripped and not stripped.startswith('/*') and not stripped.startswith('*') and not stripped.startswith('//'):
        insert_at = i
        break

lines.insert(insert_at, pragma.rstrip())
new = '\n'.join(lines)

with open(path, 'w') as f:
    f.write(new)
print(f"Patched: {path}")
PYEOF
fi

echo "eCos etharp/ip warning patches done."
