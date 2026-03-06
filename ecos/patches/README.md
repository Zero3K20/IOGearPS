# eCos Source Patches

This directory contains shell scripts that patch the upstream eCos 3.0 source
tree to fix compilation errors and bugs encountered when building firmware for
the IOGear GPSU21 (MediaTek MT7688).

The CI workflow (`release.yml`) downloads the official eCos 3.0 release tarball
and then applies every patch in this directory (in lexicographic order) before
building the eCos kernel library.

## Patch file naming

Patches are numbered to control application order:

```
NNNN-short-description.sh
```

Example: `0001-io-eth-lwip-compat.sh`

## Applying patches (CI)

The CI applies all patches automatically:

```yaml
- name: Apply eCos patches from repository
  run: |
    for patch in ecos/patches/*.sh; do
      echo "Applying $patch …"
      bash "$patch" ecos-3.0
    done
```

## Applying patches (local development)

After downloading and extracting `ecos-3.0/`:

```bash
for patch in ecos/patches/*.sh; do
    bash "$patch" /path/to/ecos-3.0
done
```

## Adding a new patch

1. Identify the eCos source file(s) that need modification.
2. Create a numbered shell script in this directory, e.g.
   `0002-my-fix.sh`, following the template of the existing scripts.
3. The script **must** accept the eCos source root directory as `$1`.
4. Transformations **must** be idempotent (safe to apply more than once).
5. Commit the new script; the CI will pick it up automatically on the next run.

## Existing patches

| File | Packages affected | Description |
|------|-------------------|-------------|
| `0001-io-eth-lwip-compat.sh` | `CYGPKG_IO_ETH_DRIVERS` | Guards `struct arpcom` in `eth_drv.h` and wraps `stand_alone/eth_drv.c` so both the standalone and lwIP Ethernet paths can coexist without a `redefinition of struct arpcom` error. |
| `0002-libc-stdio-gcc14-compat.sh` | `CYGPKG_LIBC_STDIO` | Removes trailing `__attribute__((nothrow))` from `extern __inline__` function definitions in `stdio.inl`. GCC 14+ rejects attributes placed after the declarator in function definitions. Removing the attribute is safe in eCos (bare-metal, exceptions disabled). |
| `0003-lwip-etharp-warnings.sh` | `CYGPKG_NET_LWIP` | Suppresses GCC `[-Wattributes]` warnings caused by `PACK_STRUCT_FIELD()` being applied to struct-typed members in `etharp.h` and `ip.h`, and suppresses `[-Wstrict-aliasing]` warnings from type-punned pointer usage in `etharp.c`. Uses `#pragma GCC diagnostic push/pop` for targeted, safe suppression. |
| `0004-lwip-opt-expansion-to-defined.sh` | `CYGPKG_NET_LWIP` | Wraps `#if LWIP_STATS` and `#if PPP_SUPPORT` in `lwip/opt.h` with `#pragma GCC diagnostic push/ignored/pop` to suppress `[-Wexpansion-to-defined]` warnings caused by eCos CDL-generated macros that expand to expressions containing the `defined` operator. |
| `0005-lwip-igmp-cdl.sh` | `CYGPKG_NET_LWIP` | Adds the `CYGPKG_LWIP_IGMP` CDL component to `lwip_net.cdl` (absent from the v3_0 release, present in the post-release "current" tree). Without this, `ecosconfig check` fails with "unknown component `CYGPKG_LWIP_IGMP`". Also adds the corresponding `#ifdef CYGPKG_LWIP_IGMP / #define LWIP_IGMP 1` block to `lwipopts.h` so the IGMP module is enabled at compile time (required by the mDNS stack to join multicast group 224.0.0.251). |
