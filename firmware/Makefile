# Top-level Makefile for the IOGear GPSU21 eCos firmware build.
#
# Prerequisites:
#   - ECOS_REPOSITORY  set to a colon-separated list that includes BOTH:
#       1. the mainline eCos packages directory  (from the eCos git clone)
#       2. $(TOP_DIR)/hal                        (the MT7688 HAL in this repo)
#     Example (CI):
#       export ECOS_REPOSITORY=/workspace/ecos-src/packages:/workspace/firmware/hal
#   - mipsel-elf-gcc   (or mipsel-linux-gnu-gcc) on $PATH
#   - ecosconfig       on $PATH  (built from ecos-src/host)
#   - Python 3 with the 'lzma' module (stdlib — no extra packages needed)
#
# Usage:
#   make              # build everything (kernel + app + packaged firmware)
#   make ecos-build   # configure and build the eCos kernel library only
#   make app-build    # build the application against the kernel library
#   make firmware     # package eCos ELF into a flashable .bin
#   make clean        # remove all build artefacts

# ──────────────────────────────────────────────────────────────────────────────
# Toolchain — override on the command line if needed:
#   make CROSS_COMPILE=mipsel-linux-gnu-
# ──────────────────────────────────────────────────────────────────────────────
CROSS_COMPILE ?= mipsel-elf-
CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy

# ──────────────────────────────────────────────────────────────────────────────
# Directories
# ──────────────────────────────────────────────────────────────────────────────
TOP_DIR     := $(CURDIR)
BUILD_DIR   := $(TOP_DIR)/build
ECOS_BUILD  := $(TOP_DIR)/ecos_build
SRC_DIR     := $(TOP_DIR)/src
WEB_DIR     := $(TOP_DIR)/../gpsu21_web
# MT7688 HAL — provided in this repository (no trailing spaces: Make includes them in variable values)
HAL_DIR     := $(TOP_DIR)/hal

ECC_FILE    := $(TOP_DIR)/ecos.ecc
PKG_SCRIPT  := $(TOP_DIR)/package_firmware.py

ELF_OUT     := $(BUILD_DIR)/gpsu21_app.elf
BIN_OUT     := $(BUILD_DIR)/gpsu21_app.bin
FW_OUT      := $(BUILD_DIR)/gpsu21_ecos.bin

# Extend ECOS_REPOSITORY to include our custom HAL packages.
# ECOS_REPOSITORY may already be set by the caller (e.g. CI); we append
# the firmware/hal directory so ecosconfig can find CYGPKG_HAL_MIPS_RALINK_MT7688.
export ECOS_REPOSITORY := $(ECOS_REPOSITORY):$(HAL_DIR)

# ──────────────────────────────────────────────────────────────────────────────
# Default target
# ──────────────────────────────────────────────────────────────────────────────
.PHONY: all
all: firmware

# ──────────────────────────────────────────────────────────────────────────────
# Step 1 — Build the eCos kernel library
# ──────────────────────────────────────────────────────────────────────────────
.PHONY: ecos-build
ecos-build: $(ECOS_BUILD)/install/lib/libtarget.a

$(ECOS_BUILD)/install/lib/libtarget.a: $(ECC_FILE)
	@echo "=== Configuring eCos for MT7688 (ECOS_REPOSITORY=$(ECOS_REPOSITORY)) ==="
	mkdir -p $(ECOS_BUILD)
	cd $(ECOS_BUILD) && ecosconfig import $(ECC_FILE)
	cd $(ECOS_BUILD) && ecosconfig check
	$(MAKE) -C $(ECOS_BUILD)
	@echo "=== eCos kernel library built ==="

# ──────────────────────────────────────────────────────────────────────────────
# Step 2 — Build the application
# ──────────────────────────────────────────────────────────────────────────────
.PHONY: app-build
app-build: ecos-build $(ELF_OUT)

$(ELF_OUT): $(ECOS_BUILD)/install/lib/libtarget.a
	mkdir -p $(BUILD_DIR)
	$(MAKE) -C $(SRC_DIR) \
	    CROSS_COMPILE=$(CROSS_COMPILE) \
	    ECOS_INSTALL=$(ECOS_BUILD)/install \
	    WEB_DIR=$(WEB_DIR) \
	    OUTPUT=$(ELF_OUT)

# ──────────────────────────────────────────────────────────────────────────────
# Step 3 — Convert ELF → raw binary
# ──────────────────────────────────────────────────────────────────────────────
$(BIN_OUT): $(ELF_OUT)
	$(OBJCOPY) -O binary $< $@
	@echo "  Raw binary: $@ ($(shell stat -c%s $@) bytes)"

# ──────────────────────────────────────────────────────────────────────────────
# Step 4 — Package into GPSU21 flashable .bin
# ──────────────────────────────────────────────────────────────────────────────
.PHONY: firmware
firmware: $(FW_OUT)

$(FW_OUT): $(BIN_OUT)
	python3 $(PKG_SCRIPT) $(BIN_OUT) $(FW_OUT)
	@echo ""
	@echo "  Firmware image: $(FW_OUT)"
	@echo "  Flash via: GPSU21 web interface → System → Upgrade"

# ──────────────────────────────────────────────────────────────────────────────
# Clean
# ──────────────────────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	$(MAKE) -C $(SRC_DIR) clean 2>/dev/null || true
	rm -rf $(BUILD_DIR) $(ECOS_BUILD)
	@echo "Clean done."
