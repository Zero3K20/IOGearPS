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
