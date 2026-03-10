#!/bin/sh
# build_openwrt_gpsu21.sh — Build an OpenWrt initramfs image for the IOGear GPSU21
#
# The GPSU21 uses a MediaTek MT7688 SoC (MIPS32r2 little-endian, 64 MB RAM).
# OpenWrt supports this SoC under the ramips/mt76x8 target.
#
# KEY REQUIREMENT — kernel load address
# ──────────────────────────────────────
# The GPSU21's ZOT Technology bootloader always decompresses firmware to DRAM
# address 0x80500000 (KSEG0 virtual, physical 0x00500000) and jumps there.
# A standard OpenWrt MT7688 kernel is compiled for 0x80000000 and will crash
# immediately on this device.  This script patches the kernel load address
# before building.
#
# Usage
# ─────
#   ./scripts/build_openwrt_gpsu21.sh [--openwrt-dir <path>] [--package <pkg>]
#
# Options:
#   --openwrt-dir <path>   Path to an existing OpenWrt source tree.
#                          If not specified, the script clones OpenWrt into
#                          ./openwrt in the current directory.
#   --package <pkg>        Extra OpenWrt package to include (may be repeated).
#                          Default: p910nd (lightweight print server).
#   --help                 Show this help.
#
# After the build completes the initramfs kernel image is at:
#   <openwrt-dir>/bin/targets/ramips/mt76x8/openwrt-ramips-mt76x8-*-initramfs-kernel.bin
#
# Then wrap it in the GPSU21 ZOT firmware format:
#   python3 tools/package_openwrt_gpsu21.py \
#       <openwrt-dir>/bin/targets/ramips/mt76x8/openwrt-ramips-mt76x8-*-initramfs-kernel.bin \
#       gpsu21_openwrt.bin
#
# And flash gpsu21_openwrt.bin via the GPSU21 web interface (System → Upgrade).
#
# References:
#   https://openwrt.org/docs/guide-developer/toolchain/use-buildsystem
#   https://openwrt.org/docs/techref/targets/ramips

set -e

# ── Defaults ─────────────────────────────────────────────────────────────────
OPENWRT_DIR=""
EXTRA_PACKAGES=""
OPENWRT_BRANCH="main"
OPENWRT_REPO="https://git.openwrt.org/openwrt/openwrt.git"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

# ── Argument parsing ──────────────────────────────────────────────────────────
while [ $# -gt 0 ]; do
    case "$1" in
        --openwrt-dir)
            shift
            OPENWRT_DIR="$1"
            shift
            ;;
        --package)
            shift
            EXTRA_PACKAGES="$EXTRA_PACKAGES $1"
            shift
            ;;
        --help|-h)
            sed -n '2,/^set -e/{ s/^# \{0,1\}//; /^set -e/d; p }' "$0"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            echo "Run '$0 --help' for usage." >&2
            exit 1
            ;;
    esac
done

# ── Helper ────────────────────────────────────────────────────────────────────
need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Error: '$1' is required but not found in PATH." >&2
        exit 1
    fi
}
need_cmd git
need_cmd make
need_cmd python3

# ── Step 1: Obtain OpenWrt source ─────────────────────────────────────────────
if [ -z "$OPENWRT_DIR" ]; then
    OPENWRT_DIR="$(pwd)/openwrt"
    if [ ! -d "$OPENWRT_DIR/.git" ]; then
        echo "==> Cloning OpenWrt ($OPENWRT_BRANCH) into $OPENWRT_DIR …"
        git clone --depth 1 --branch "$OPENWRT_BRANCH" "$OPENWRT_REPO" "$OPENWRT_DIR"
    else
        echo "==> Using existing OpenWrt tree at $OPENWRT_DIR"
    fi
else
    echo "==> Using OpenWrt tree at $OPENWRT_DIR"
fi

# ── Step 2: Install OpenWrt feeds ─────────────────────────────────────────────
echo "==> Updating and installing OpenWrt feeds …"
cd "$OPENWRT_DIR"
./scripts/feeds update -a
./scripts/feeds install -a

# ── Step 3: Apply GPSU21 config fragment ──────────────────────────────────────
echo "==> Applying GPSU21 config fragment …"
cp "$REPO_ROOT/scripts/openwrt_gpsu21.config" "$OPENWRT_DIR/.config"

# Append any extra packages requested on the command line
for pkg in $EXTRA_PACKAGES; do
    echo "CONFIG_PACKAGE_${pkg}=y" >> "$OPENWRT_DIR/.config"
done

# Expand the config to fill in all defaults
make defconfig

# ── Step 4: Patch kernel load address ─────────────────────────────────────────
# The GPSU21 ZOT bootloader decompresses firmware to 0x80500000 (physical
# 0x00500000).  We need two things:
#   a) The Linux kernel to be linked at 0x80500000 so all symbol references
#      are correct.  This is set via CONFIG_PHYSICAL_START in the kernel .config.
#   b) The uImage load/entry address fields to say 0x80500000 so that
#      package_openwrt_gpsu21.py knows where the kernel expects to run.
#
# OpenWrt exposes the uImage load address as KERNEL_LOADADDR which flows into
# arch/mips/configs/<platform>_defconfig and into mkimage -a/-e.  The kernel
# physical start is CONFIG_PHYSICAL_START in the kernel Kconfig.

KERNEL_CONFIG="$OPENWRT_DIR/target/linux/ramips/mt76x8/config-6.6"
if [ ! -f "$KERNEL_CONFIG" ]; then
    # Try other kernel version directories (OpenWrt may use 6.1, 5.15, etc.)
    KERNEL_CONFIG="$(ls "$OPENWRT_DIR/target/linux/ramips/mt76x8/config-"* 2>/dev/null | head -1)"
fi

if [ -n "$KERNEL_CONFIG" ] && [ -f "$KERNEL_CONFIG" ]; then
    echo "==> Patching kernel config: $KERNEL_CONFIG"
    # Remove any existing PHYSICAL_START setting and add ours
    sed -i '/CONFIG_PHYSICAL_START/d' "$KERNEL_CONFIG"
    echo "CONFIG_PHYSICAL_START=0x00500000" >> "$KERNEL_CONFIG"
    echo "  Set CONFIG_PHYSICAL_START=0x00500000 (virtual load 0x80500000)"
else
    echo "WARNING: Could not find ramips/mt76x8 kernel config; skipping patch."
    echo "  You may need to manually set CONFIG_PHYSICAL_START=0x00500000 before booting."
fi

# Override the KERNEL_LOADADDR make variable for the ramips image Makefile so
# that mkimage embeds 0x80500000 as the uImage load and entry address.
# We inject this via a temporary include that target/linux/ramips/image/*.mk
# will pick up through the standard KERNEL_LOADADDR override mechanism.
IMAGEBUILDER_INCLUDES="$OPENWRT_DIR/include/image-commands.mk"
if grep -q "KERNEL_LOADADDR" "$IMAGEBUILDER_INCLUDES" 2>/dev/null; then
    echo "==> KERNEL_LOADADDR already present in $IMAGEBUILDER_INCLUDES"
else
    # Append to the ramips/mt76x8 image Makefile via the global override
    RAMIPS_IMAGE_MK="$OPENWRT_DIR/target/linux/ramips/image/mt76x8.mk"
    if [ -f "$RAMIPS_IMAGE_MK" ]; then
        echo "==> Patching $RAMIPS_IMAGE_MK to set KERNEL_LOADADDR=0x80500000"
        # Only add if not already set for the generic/gpsu21 target
        if ! grep -q "GPSU21\|gpsu21" "$RAMIPS_IMAGE_MK"; then
            cat >> "$RAMIPS_IMAGE_MK" <<'MKEOF'

# ── IOGear GPSU21 target (added by build_openwrt_gpsu21.sh) ─────────────────
# Load address 0x80500000 required by the ZOT bootloader.
define Device/gpsu21
  $(Device/Default)
  DEVICE_VENDOR := IOGear
  DEVICE_MODEL  := GPSU21
  SOC           := mt7688
  KERNEL_SIZE   := 6144k
  KERNEL_LOADADDR := 0x80500000
  KERNEL := kernel-bin | append-dtb | lzma | uImage lzma
  IMAGES  := initramfs-kernel.bin
  IMAGE/initramfs-kernel.bin := append-kernel
  SUPPORTED_DEVICES += ioGear,gpsu21
endef
TARGET_DEVICES += gpsu21
MKEOF
            echo "  Added gpsu21 device entry with KERNEL_LOADADDR=0x80500000"
        fi
    fi
fi

# ── Step 5: Build ─────────────────────────────────────────────────────────────
echo "==> Building OpenWrt for ramips/mt76x8 (this may take 30–90 minutes) …"
# V=s prints each compiler/linker command on one line (simplified output).
# Change to V=sc to also show stdout, or omit V entirely for quiet output.
make -j"$(nproc)" V=s

# ── Step 6: Locate the output image ───────────────────────────────────────────
BINS_DIR="$OPENWRT_DIR/bin/targets/ramips/mt76x8"
INITRAMFS_IMG="$(ls "$BINS_DIR"/openwrt-ramips-mt76x8-gpsu21-initramfs-kernel.bin 2>/dev/null || \
                ls "$BINS_DIR"/openwrt-ramips-mt76x8-generic-initramfs-kernel.bin 2>/dev/null || \
                echo "")"

echo ""
echo "══════════════════════════════════════════════════════════════"
echo "  Build complete!"
echo ""
if [ -n "$INITRAMFS_IMG" ] && [ -f "$INITRAMFS_IMG" ]; then
    echo "  Initramfs kernel image:"
    echo "    $INITRAMFS_IMG"
    echo "    Size: $(wc -c < "$INITRAMFS_IMG") bytes"
    echo ""
    echo "  Wrap for the GPSU21 ZOT bootloader:"
    echo "    python3 $REPO_ROOT/tools/package_openwrt_gpsu21.py \\"
    echo "        \"$INITRAMFS_IMG\" \\"
    echo "        gpsu21_openwrt.bin"
    echo ""
    echo "  Flash gpsu21_openwrt.bin via the GPSU21 web interface:"
    echo "    http://<printer-ip>/ → System → Upgrade"
else
    echo "  Could not locate initramfs kernel image in $BINS_DIR"
    echo "  Check build output above for errors."
    echo ""
    echo "  Images produced:"
    ls "$BINS_DIR"/*.bin 2>/dev/null | sed 's/^/    /' || echo "    (none)"
fi
echo "══════════════════════════════════════════════════════════════"
