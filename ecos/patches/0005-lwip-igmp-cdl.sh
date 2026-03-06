#!/usr/bin/env bash
# Patch: add CYGPKG_LWIP_IGMP component to eCos 3.0 v3_0 lwIP CDL
#
# Problem
# -------
# The official eCos 3.0 release tarball's v3_0 lwIP CDL
# (packages/net/lwip_tcpip/v3_0/cdl/lwip_net.cdl) does not define the
# CYGPKG_LWIP_IGMP component.  This component was added to the lwIP CDL
# after the 3.0 release (it is present in the post-release "current" tree).
#
# When firmware/ecos.ecc contains a cdl_component entry for CYGPKG_LWIP_IGMP,
# ecosconfig fails with:
#
#   ecos.ecc: error
#       The savefile contains a cdl_component command for an unknown
#       component `CYGPKG_LWIP_IGMP'
#
# Additionally, without the CDL component, the v3_0 lwipopts.h does not
# define LWIP_IGMP=1, so the lwIP IGMP module is disabled even though
# igmp.c is compiled.  IGMP is required by the mDNS stack (mdns.c) to
# join the mDNS multicast group (224.0.0.251) via IP_ADD_MEMBERSHIP.
#
# Fix
# ---
# 1. Add cdl_component CYGPKG_LWIP_IGMP to lwip_net.cdl (after
#    CYGPKG_LWIP_ICMP) so that ecosconfig recognises the component.
#
# 2. Add an IGMP section to lwipopts.h so that defining CYGPKG_LWIP_IGMP
#    in pkgconf/net_lwip.h causes LWIP_IGMP to be set to 1.
#
# Usage
# -----
#   bash ecos/patches/0005-lwip-igmp-cdl.sh <ecos-src-root>
#
# Idempotency
# -----------
# A sentinel string is checked before any change is made; re-running the
# script against an already-patched file is a no-op.

set -euo pipefail

ECOS_ROOT="${1:?Usage: $0 <ecos-src-root>}"

CDL="${ECOS_ROOT}/packages/net/lwip_tcpip/v3_0/cdl/lwip_net.cdl"
LWIPOPTS="${ECOS_ROOT}/packages/net/lwip_tcpip/v3_0/include/lwipopts.h"

for f in "$CDL" "$LWIPOPTS"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: expected file not found: $f" >&2
        exit 1
    fi
done

# ── Part 1: add CYGPKG_LWIP_IGMP to lwip_net.cdl ────────────────────────────

SENTINEL_CDL='CYGPKG_LWIP_IGMP'

if grep -q "$SENTINEL_CDL" "$CDL"; then
    echo "Already patched (skipping CDL): $CDL"
else
    python3 - "$CDL" <<'PYEOF'
import sys, re

path = sys.argv[1]

with open(path, 'r') as f:
    content = f.read()

# Check sentinel again (idempotency inside Python)
if 'CYGPKG_LWIP_IGMP' in content:
    print(f"Already patched (skipping): {path}")
    sys.exit(0)

# The IGMP component block to insert, matching the style used in the
# post-release "current" CDL (antmicro/ecos).
igmp_component = '''
        cdl_component CYGPKG_LWIP_IGMP {
            display         "IGMP support"
            flavor          bool
            default_value   1
            description     "
                Support for IGMP functionality."
        }
'''

# Insert after the closing brace of the CYGPKG_LWIP_ICMP component block.
# The ICMP block ends with a single "}" on its own line inside the PROTOCOLS
# component.  We find the last "}" that closes CYGPKG_LWIP_ICMP by locating
# the component definition and then its matching closing brace.
#
# Strategy: find "cdl_component CYGPKG_LWIP_ICMP {", then walk forward to
# find its matching "}" (tracking brace depth), then insert after it.

icmp_start = content.find('cdl_component CYGPKG_LWIP_ICMP')
if icmp_start == -1:
    print(f"WARNING: CYGPKG_LWIP_ICMP not found in {path}; inserting at end of PROTOCOLS block")
    # Fall back: insert just before the closing brace of cdl_component CYGPKG_LWIP_PROTOCOLS.
    # Use CYGPKG_LWIP_UDP as an upper boundary if it exists; otherwise search the whole file.
    udp_pos = content.find('cdl_component CYGPKG_LWIP_UDP')
    search_limit = udp_pos if udp_pos != -1 else len(content)
    protocols_close = content.rfind('\n        }', 0, search_limit)
    if protocols_close == -1:
        print(f"ERROR: could not find insertion point in {path}")
        sys.exit(1)
    new = content[:protocols_close] + igmp_component + content[protocols_close:]
else:
    # Walk brace depth to find closing brace of CYGPKG_LWIP_ICMP
    depth = 0
    i = content.find('{', icmp_start)
    if i == -1:
        print(f"ERROR: no opening brace for CYGPKG_LWIP_ICMP in {path}")
        sys.exit(1)
    while i < len(content):
        ch = content[i]
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                # i is the closing brace of CYGPKG_LWIP_ICMP
                # Find end of this line
                eol = content.find('\n', i)
                if eol == -1:
                    eol = len(content)
                new = content[:eol + 1] + igmp_component + content[eol + 1:]
                break
        i += 1
    else:
        print(f"ERROR: could not find closing brace for CYGPKG_LWIP_ICMP in {path}")
        sys.exit(1)

with open(path, 'w') as f:
    f.write(new)
print(f"Patched: {path}")
PYEOF
fi

# ── Part 2: add IGMP section to lwipopts.h ───────────────────────────────────

SENTINEL_LWIPOPTS='LWIP_IGMP'

if grep -q "$SENTINEL_LWIPOPTS" "$LWIPOPTS"; then
    echo "Already patched (skipping lwipopts.h): $LWIPOPTS"
else
    python3 - "$LWIPOPTS" <<'PYEOF'
import sys, re

path = sys.argv[1]

with open(path, 'r') as f:
    content = f.read()

if 'LWIP_IGMP' in content:
    print(f"Already patched (skipping): {path}")
    sys.exit(0)

# The IGMP section to add, matching the style of lwipopts.h (antmicro/ecos).
igmp_section = """
//------------------------------------------------------------------------------
// IGMP options
//------------------------------------------------------------------------------

#ifdef CYGPKG_LWIP_IGMP
# define LWIP_IGMP                  1
#endif

"""

# Insert just before the SNMP or DNS section; fall back to appending.
insert_pos = -1
for anchor in ['// DNS options', '// SNMP options', '#endif /* CYGONCE_LWIPOPTS_H */']:
    insert_pos = content.find(anchor)
    if insert_pos != -1:
        break

if insert_pos != -1:
    new = content[:insert_pos] + igmp_section + content[insert_pos:]
else:
    new = content + igmp_section

with open(path, 'w') as f:
    f.write(new)
print(f"Patched: {path}")
PYEOF
fi

echo "eCos lwIP IGMP CDL/lwipopts patch done."
