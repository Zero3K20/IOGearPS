#!/usr/bin/env bash
# Patch: fix libc/stdio inline functions for GCC 14 compatibility
#
# Problem
# -------
# eCos 3.0 packages/language/c/libc/stdio/v3_0/include/stdio.inl contains
# inline function definitions of the form:
#
#   extern __inline__ TYPE
#   function_name (params) __THROW
#   {
#       ...
#   }
#
# where __THROW is a macro (from <sys/cdefs.h>) that expands to
# __attribute__((__nothrow__)) in C mode with GCC.
#
# GCC 14 made the placement of __attribute__ after the declarator (i.e. after
# the function name and parameter list) in a function DEFINITION a hard error:
#
#   error: attributes should be specified before the declarator in a
#          function definition
#
# Previously (GCC ≤ 13) this was only a warning or was silently accepted.
#
# Fix
# ---
# Remove trailing __THROW / __THROWNL / __attribute__((...)) tokens from the
# end of the function signature in inline function definitions (the line
# immediately before the opening brace).  In eCos — a bare-metal RTOS with
# C++ exceptions disabled — these attributes (nothrow/leaf) are no-ops, so
# removing them is safe.
#
# The transformation uses Python's re.sub so the logic is readable and
# correct for all affected lines regardless of whitespace layout.
#
# Usage
# -----
#   bash ecos/patches/0002-libc-stdio-gcc14-compat.sh <ecos-src-root>
#
# Idempotency
# -----------
# Python re.sub reports whether it made any changes; if the pattern no longer
# matches in the already-patched file, the script prints "Already patched".

set -euo pipefail

ECOS_ROOT="${1:?Usage: $0 <ecos-src-root>}"

STDIO_INL="${ECOS_ROOT}/packages/language/c/libc/stdio/v3_0/include/stdio.inl"

if [ ! -f "$STDIO_INL" ]; then
    echo "ERROR: expected file not found: $STDIO_INL" >&2
    exit 1
fi

python3 - "$STDIO_INL" <<'PYEOF'
import re, sys

inl_path = sys.argv[1]

with open(inl_path, 'r') as f:
    content = f.read()

# Match: closing paren of parameter list followed (optionally across whitespace)
# by an attribute token immediately before the opening brace of the body.
#
# eCos 3.0 stdio.inl uses the __THROW macro (from <sys/cdefs.h>) which
# expands to __attribute__((__nothrow__)) in C mode with GCC.  The source
# text therefore reads:
#
#   ) __THROW
#   {
#
# rather than the expanded form.  We match both the raw macro names and the
# expanded __attribute__((...)) form so the patch is robust against either
# style appearing in the source.
#
# Patterns removed:
#   __THROW                         — eCos stdio.inl (GCC/glibc C mode)
#   __THROWNL                       — variant without __leaf__
#   __attribute__((<attrs>))        — expanded form (no nested parens)
new_content = re.sub(
    r'\)\s*(?:__attribute__\s*\(\([^()]*\)\)|__THROW|__THROWNL)(\s*\n\{)',
    r')\1',
    content
)

if new_content != content:
    with open(inl_path, 'w') as f:
        f.write(new_content)
    print(f"Patched: {inl_path}")
else:
    print(f"Already patched (skipping): {inl_path}")
PYEOF
