# Building FreeRTOS Firmware for the IOGear GPSU21

This directory contains everything needed to build a compatible firmware image
for the **IOGear GPSU21 print server** using FreeRTOS.

The GPSU21 uses a **MediaTek MT7688** MIPS SoC.  The sources here re-implement
the same print-server application on top of FreeRTOS V10.6.2 and lwIP 2.2.0 so
that the resulting binary can be flashed using the device's standard firmware
upgrade page.

---

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| FreeRTOS-Kernel | V10.6.2 | From https://github.com/FreeRTOS/FreeRTOS-Kernel |
| lwIP | 2.2.0 | From https://github.com/lwip-tcpip/lwip |
| MIPS cross-compiler | `mipsel-linux-gnu-gcc` 9.x+ | Ubuntu/Debian package or crosstool-ng |
| Python 3 | 3.8+ | For `package_firmware.py` |
| `lzma` / `xz-utils` | any | LZMA compression |

### Install the MIPS cross-toolchain (Ubuntu / Debian)

```sh
sudo apt-get install gcc-mipsel-linux-gnu binutils-mipsel-linux-gnu
```

### Obtain FreeRTOS-Kernel and lwIP

```sh
curl -fL https://github.com/FreeRTOS/FreeRTOS-Kernel/archive/refs/tags/V10.6.2.tar.gz \
  | tar -xz && mv FreeRTOS-Kernel-* freertos-kernel

curl -fL https://github.com/lwip-tcpip/lwip/archive/refs/tags/STABLE-2_2_0_RELEASE.tar.gz \
  | tar -xz && mv lwip-* lwip
```

---

## Directory layout

```
firmware/
├── README.md               ← this file
├── Makefile                ← top-level build automation
├── package_firmware.py     ← packages the ELF binary into a flashable GPSU21 .bin
├── bsp/                    ← MT7688 board support (startup, UART, init, linker script)
│   └── freertos_port/      ← custom MIPS32r2 FreeRTOS port
├── freertos/               ← lwIP port and MT7688 Ethernet driver
│   ├── lwip_port/          ← lwIP sys_arch (FreeRTOS ↔ lwIP glue)
│   └── netif/              ← MT7688 RAETH PDMA Ethernet driver
└── src/
    ├── main.c              ← application entry point, thread creation
    ├── httpd.c             ← minimal HTTP server (serves web UI from ROM table)
    ├── ipp_server.c        ← IPP/AirPrint server (port 631)
    ├── escl_server.c       ← eSCL/AirScan server (port 9290)
    ├── wsd_server.c        ← WSD discovery server (UDP 3702 / TCP 5357)
    ├── mdns.c              ← mDNS/Bonjour advertising
    ├── lpr.c               ← LPR/LPD print server (port 515)
    ├── config.c            ← persistent device configuration
    └── usb_printer.c       ← USB printer forwarding
```

---

## Quick start

```sh
# Build (from the repository root, with freertos-kernel and lwip extracted alongside)
make -C firmware \
  CROSS_COMPILE=mipsel-linux-gnu- \
  FREERTOS_DIR=$(pwd)/freertos-kernel \
  LWIP_DIR=$(pwd)/lwip

# Output: firmware/build/gpsu21_freertos.bin
```

Flash the output file via the GPSU21 web interface:
**System → Upgrade → browse → select `firmware/build/gpsu21_freertos.bin` → Upload**

---

## Build details

### Step 1 — Compile the firmware

The top-level `Makefile` compiles FreeRTOS, lwIP, the MT7688 BSP, and the
application sources in a single pass, producing a MIPS ELF binary
(`build/gpsu21_app.elf`).

Key linker settings:

| Setting | Value | Reason |
|---|---|---|
| Load address | `0x80000400` | MT7688 DRAM start used by ZOT bootloader |
| Entry point | `_start` (BSP startup) | Low-level MIPS init before FreeRTOS scheduler |
| Output format | `binary` | Flat binary for LZMA compression |

### Step 2 — Package the firmware

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
  0x4AC0 – end      LZMA-compressed firmware binary
```

Usage:

```sh
python3 firmware/package_firmware.py  build/gpsu21_app.bin  build/gpsu21_freertos.bin
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
    build/gpsu21_app.bin  build/gpsu21_freertos.bin \
    --version "MT7688-1.00.00.0001.00000001t-2026/01/01 00:00:00"
```

The bootloader accepts any version string that begins with `MT7688-`.

---

## Web interface

The web UI files are compiled into a ROM file table embedded in the firmware
binary.  The source files live in `gpsu21_web/`.  The `src/gen_web_resources.py`
script generates `build/web_resources.c` from that directory automatically as
part of the build.

---

## Flashing

1. Open `http://<printer-ip>/` in a browser.
2. Navigate to the firmware upgrade page.
3. Upload `firmware/build/gpsu21_freertos.bin`.
4. Wait ~60 seconds.  **Do not power-cycle during the upgrade.**

To recover a bricked device, see the **Emergency Recovery** section in the
top-level `README.md`.
