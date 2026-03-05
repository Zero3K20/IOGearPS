# Building eCos Firmware for the IOGear GPSU21

This directory contains everything needed to build a compatible firmware image
for the **IOGear GPSU21 print server** from eCos RTOS source.

The GPSU21 uses a **MediaTek MT7688** MIPS SoC and runs an eCos-based firmware
built by ZOT Technology.  The sources here re-implement the same print-server
application on top of a stock eCos kernel so that the resulting binary can be
flashed using the device's standard firmware upgrade page.

---

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| eCos RTOS source | 3.0 | From https://ecos.sourceware.org/ |
| MIPS cross-compiler | `mipsel-elf-gcc` 9.x+ | Buildroot or crosstool-ng |
| Python 3 | 3.8+ | For `package_firmware.py` |
| `lzma` / `xz-utils` | any | LZMA compression |

### Install the MIPS cross-toolchain (Ubuntu / Debian)

```sh
sudo apt-get install gcc-mipsel-linux-gnu binutils-mipsel-linux-gnu
# or use a bare-metal (elf) toolchain from crosstool-ng
```

### Obtain eCos

```sh
git clone https://git.code.sf.net/p/ecos/ecos ecos-src
export ECOS_REPOSITORY=$PWD/ecos-src/packages
```

---

## Directory layout

```
firmware/
├── README.md           ← this file
├── Makefile            ← top-level build automation
├── ecos.ecc            ← exported eCos component configuration for MT7688
├── package_firmware.py ← packages the eCos ELF binary into a flashable GPSU21 .bin
└── src/
    ├── Makefile        ← eCos application build file
    ├── main.c          ← application entry point, thread creation
    ├── httpd.c         ← minimal HTTP server (serves web UI from ROM table)
    ├── ipp_server.c    ← IPP/AirPrint server (port 631)
    ├── mdns.c          ← mDNS/Bonjour advertising (_ipp._tcp)
    └── lpr.c           ← LPR/LPD print server (port 515)
```

---

## Quick start

```sh
# 1. Configure and build the eCos kernel library
make ecos-build

# 2. Build the application against the kernel library
make app-build

# 3. Package the ELF into a GPSU21-compatible .bin
make firmware

# Output: build/gpsu21_ecos.bin
```

Flash the output file via the GPSU21 web interface:
**System → Upgrade → browse → select `build/gpsu21_ecos.bin` → Upload**

---

## Build details

### Step 1 — Configure eCos for MT7688

The supplied `ecos.ecc` selects the correct HAL packages for the MT7688:

| eCos package | Purpose |
|---|---|
| `CYGPKG_HAL_MIPS` | MIPS architecture HAL |
| `CYGPKG_HAL_MIPS_RALINK_MT7688` | MT7688 board HAL (memory map, clocks, Ethernet) |
| `CYGPKG_KERNEL` | eCos RTOS kernel (threads, mutexes, semaphores) |
| `CYGPKG_IO_ETH_DRIVERS` | Ethernet I/O framework |
| `CYGPKG_NET_LWIP` | lwIP TCP/IP stack |
| `CYGPKG_COMPRESS_LZMA` | LZMA support (for ROM table) |

The `ecosconfig` tool reads `ecos.ecc` and generates the `ecos_build/` tree:

```sh
ecosconfig import ecos.ecc
ecosconfig check
make -C ecos_build
```

This produces `ecos_build/install/lib/libtarget.a` — the kernel library.

### Step 2 — Build the application

The `src/Makefile` compiles the application sources against the generated
`libtarget.a` and produces a MIPS ELF binary (`build/gpsu21_app.elf`).

Key linker settings:

| Setting | Value | Reason |
|---|---|---|
| Load address | `0x80000400` | MT7688 DRAM start used by ZOT firmware |
| Entry point | `cyg_user_start` | eCos application entry |
| Output format | `binary` | Flat binary for LZMA compression |

### Step 3 — Package the firmware

`package_firmware.py` takes the raw binary and wraps it in the layers that
the GPSU21 bootloader expects:

```
flashable .bin layout (same as OEM firmware)
─────────────────────────────────────────────
  0x0000 – 0x00FF   256-byte ZOT Technology firmware header
                    (version string, size, CRC32)
  0x0100 – 0x013F   64-byte U-Boot uImage header
                    (MIPS standalone, CRC32 over payload)
  0x0140 – 0x4ABF   Padding / extended header region (all 0xFF)
  0x4AC0 – end      LZMA-compressed eCos binary
```

Usage:

```sh
python3 firmware/package_firmware.py  build/gpsu21_app.bin  build/gpsu21_ecos.bin
```

---

## Firmware version string

The ZOT header embeds a version string at offset 0x28.  The default used by
`package_firmware.py` is:

```
MT7688-9.09.56.9034.00001243t-2019/11/19 13:00:10
```

Pass `--version` to override:

```sh
python3 firmware/package_firmware.py \
    build/gpsu21_app.bin  build/gpsu21_ecos.bin \
    --version "MT7688-1.00.00.0001.00000001t-2026/01/01 00:00:00"
```

The bootloader accepts any version string that begins with `MT7688-`.

---

## Web interface

The web UI files are compiled into the ROM file table at the end of the eCos
binary (see `unpack_gpsu21.py` / `repack_gpsu21.py` in `tools/`).  The
source files live in `gpsu21_web/`.  The `src/Makefile` generates the C
array (`web_resources.c`) from that directory automatically.

---

## Flashing

1. Open `http://<printer-ip>/` in a browser.
2. Navigate to the firmware upgrade page.
3. Upload `build/gpsu21_ecos.bin`.
4. Wait ~60 seconds.  **Do not power-cycle during the upgrade.**

To recover a bricked device, see the **Emergency Recovery** section in the
top-level `README.md`.
