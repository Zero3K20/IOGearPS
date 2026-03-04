# IOGear PS-1206U and GPSU21 Print Server Firmware

This repository contains firmware for two **different** IOGear print servers.
See [COMPATIBILITY.md](COMPATIBILITY.md) for a full hardware comparison.

| Device | Firmware file | CPU |
|--------|--------------|-----|
| IOGear PS-1206U | `PS-1206U_v8.8.bin` | Edimax 8820 (x86) |
| IOGear GPSU21 | `MPS56_90956F_9034_20191119.zip` | MediaTek MT7688 (MIPS) |

> ⚠️ **These firmware images are NOT interchangeable.** Flashing PS-1206U
> firmware onto a GPSU21 (or vice versa) will brick the device.

## Supported Printing Protocols

Both the PS-1206U and the GPSU21 support the same set of printing protocols:

| Protocol | Status |
|----------|--------|
| LPR/LPD | Enabled |
| IPP (port 631) | Enabled |
| Raw TCP printing | Enabled |
| SMB/Windows printing | Enabled |
| AppleTalk/PAP | Enabled |

## AirPrint Support

The GPSU21 firmware includes a **built-in Bonjour stack and IPP server** (port 631).
It advertises itself as `_ipp._tcp` via mDNS automatically on your local network.

### No PC software needed for modern devices

| Apple device | How to print |
|---|---|
| **iOS 14+** | Goes to Settings → Wi-Fi (on same network) → AirPrint appears automatically |
| **macOS 11+ (Big Sur and later)** | System Preferences → Printers & Scanners → "+" → print server appears automatically |
| iOS 13 or earlier | Requires optional helper (see below) |
| macOS 10.15 (Catalina) or earlier | Requires optional helper (see below) |

### Setup

1. **Assign a static IP address** to the PS-1206U (via your router's DHCP
   reservation or the print server's web interface at `http://<printer-ip>/`).
2. **Enable IPP** — open the web interface, go to **Setup → Services** and
   ensure *Use IPP* is set to *Enabled*.
3. **Enable Bonjour** — on the **Setup → TCP/IP** page, set *Rendezvous (Bonjour)
   Service* to *Enabled* and enter a descriptive *Service Name* (e.g. `IOGear
   PS-1206U`).
4. **Save & Restart** the device.

The printer will now appear automatically on iOS 14+ and macOS 11+ devices that
are on the same Wi-Fi network.

### Optional: older Apple device support (iOS 13 / macOS 10.15 and earlier)

The `airprint/` directory contains files for advertising the `_universal._sub._ipp._tcp`
sub-type that older Apple clients require — choose the one for your helper OS:

| File | Platform |
|------|----------|
| `airprint/IOGear-PS1206U.service` | Linux (Avahi/mDNS daemon) |
| `airprint/windows-bonjour.bat` | Windows (Apple Bonjour) |

> **Installing software on your PC is not required for iOS 14+ / macOS 11+.**
> The helper files above are only needed if you must also support older Apple
> devices.

### Supported Document Formats

When printing via AirPrint the iOS/macOS client sends jobs in one
of the following formats, all of which are forwarded by the print server
to the attached USB printer:

- `application/pdf`
- `image/jpeg`
- `image/png`
- `image/urf` (Universal Raster Format)
- `application/octet-stream` (raw)

## Firmware Files

| File | Device | Version | Notes |
|------|--------|---------|-------|
| `PS-1206U_v8.8.bin` | IOGear PS-1206U (Edimax 8820) | v8.8 | 512 KB Edimax flat image |
| `MPS56_90956F_9034_20191119.zip` | IOGear GPSU21 (MT7688) | 9.09.56F build 9034 (2019/11/19) | Extract the `.bin` before flashing |

> ⚠️ These files target **different hardware** — see [COMPATIBILITY.md](COMPATIBILITY.md)
> for the full hardware breakdown before flashing.

## Flashing the Firmware

### IOGear PS-1206U

The upgrade page works in **any browser on any operating system** (Windows,
macOS, or Linux):

1. Open the PS-1206U web interface at `http://<printer-ip>/`.
2. Navigate to **System → Upgrade**.
3. Upload `PS-1206U_v8.8.bin` and follow the on-screen prompts.
4. Do **not** power-cycle the device during the upgrade.

> **Safe to try:** The firmware upgrade page validates the file before writing
> to flash.  If the firmware is incompatible it will display "Signature wrong"
> and refuse to flash — the device will not be bricked.

### IOGear GPSU21

1. Extract `MPS56_90956F_9034_20191119.bin` from the ZIP file.
2. Open the GPSU21 web interface at `http://<printer-ip>/`.
3. Navigate to the firmware upgrade page.
4. Upload the `.bin` file (not the ZIP).
5. Do **not** power-cycle the device during the upgrade.

---

## Unpacking, Modifying, and Repacking the Firmware

The `tools/` directory contains Python 3 scripts that let you extract the
firmware components, inspect or modify them, and reassemble a flashable image.
The scripts work on **Windows, macOS, and Linux** — Python 3 is the only
requirement.

### Prerequisites

Install Python 3 from <https://www.python.org/downloads/> if it is not
already available.

Verify the installation:

```
# Windows (Command Prompt or PowerShell)
python --version

# macOS / Linux (use python3 if python points to Python 2)
python3 --version
```

---

### IOGear GPSU21 firmware tools

The GPSU21 firmware (`MPS56_90956F_9034_20191119.zip`) uses **eCos RTOS** on
a MediaTek MT7688 MIPS SoC.  All 61 web interface files — HTML pages,
JavaScript, CSS, and JPEG/GIF images — are stored **uncompressed inside the
LZMA-compressed eCos binary** and have already been extracted to the
[`gpsu21_web/`](gpsu21_web/) directory in this repository.

#### Edit-and-release workflow (recommended)

1. **Edit** any file in `gpsu21_web/` directly on GitHub or in a local clone
   (HTML, JS, CSS files use a text editor; keep file sizes ≤ original).
2. **Commit and push** to `main`.
3. **GitHub Actions** automatically runs `repack_gpsu21.py` and publishes a
   new release containing `GPSU21_modified.bin` — ready to flash.

> **Size constraint:** each edited file must be ≤ its original size.
> Pad shorter files with spaces or HTML comments to fill the gap.
> See `tools/repack_gpsu21.py` for details.

#### Local workflow (optional)

If you prefer to work entirely offline:

```
# Step 1 — Edit files in gpsu21_web/ with any text editor
#           (HTML pages, JS, CSS — keep file sizes the same or smaller)

# Step 2 — Repack locally
# Windows:
python tools\repack_gpsu21.py  MPS56_90956F_9034_20191119.zip  gpsu21_web\  gpsu21_modified.bin
# macOS / Linux:
python3 tools/repack_gpsu21.py  MPS56_90956F_9034_20191119.zip  gpsu21_web/  gpsu21_modified.bin

# Step 3 — Flash gpsu21_modified.bin via the GPSU21 web upgrade page
```

---

### IOGear PS-1206U firmware tools

#### Firmware layout

```
0x00000   32 bytes   Header 1 ("Edimax8820 MTYPE/")
0x00020  ~240 KB     ZIP archive → PS06EPS.BIN  (main firmware, decompressed ~896 KB)
0x3bebf  ~144 KB     Zero padding
0x60000   32 bytes   Header 2 ("Edimax7420 MTYPE/")
0x60020   ~54 KB     ZIP archive → PS06UPG.BIN  (upgrade utility, decompressed 200 KB)
0x6d8cf   ~42 KB     Zero padding
0x78000   32 KB      Bootloader / ROM section (do not modify)
```

### Step 1 — Unpack

```
# Windows
python tools\unpack.py PS-1206U_v8.8.bin firmware_unpacked\

# macOS / Linux
python3 tools/unpack.py PS-1206U_v8.8.bin firmware_unpacked/
```

This creates `firmware_unpacked/` (or `firmware_unpacked\` on Windows)
containing:

| File | Description |
|------|-------------|
| `header1.bin` | 32-byte Edimax8820 header |
| `PS06EPS.BIN` | Main print-server firmware (x86 real-mode, RTXC RTOS) |
| `header2.bin` | 32-byte Edimax7420 header |
| `PS06UPG.BIN` | Upgrade / bootloader utility |
| `bootloader.bin` | ROM-resident first-stage bootloader (end of flash) |
| `layout.txt` | Human-readable flash layout description |

### Step 2 — Analyse and modify

**PS06EPS.BIN** is the main firmware.  It contains:

- Compiled 16-bit x86 real-mode code (RTXC RTOS kernel + print-server application)
- Embedded HTML/CSS/JavaScript web UI pages
- JPEG images used by the web UI
- IPP, LPD, SMB, AppleTalk/PAP print stacks
- Device configuration strings

Because the binary was compiled by a proprietary toolchain, true **recompilation
from source is not possible** without the original source code.  However, you
can:

1. **Disassemble** the code using a tool that supports 16-bit x86, for example:
   - [Ghidra](https://ghidra-sre.org/) — free, open-source; select language
     `x86:LE:16:default` when importing `PS06EPS.BIN`
   - [IDA Pro](https://hex-rays.com/ida-pro/) or
     [Binary Ninja](https://binary.ninja/) with 8086/Real-Mode support
   - [ndisasm](https://nasm.us/) — included in the
     [NASM Windows installer](https://www.nasm.us/pub/nasm/releasebuilds/?C=M;O=D);
     run from Command Prompt:
     ```
     ndisasm.exe -b 16 firmware_unpacked\PS06EPS.BIN | more
     ```

2. **Patch the binary** directly:
   - Use a hex editor (e.g. [HxD](https://mh-nexus.de/en/hxd/) on Windows,
     `010 Editor`, or `ImHex`) to change specific bytes, strings, or
     configuration values.
   - Use a Python script to automate bulk changes.

3. **Edit embedded web pages** — the HTML/CSS/JS pages are stored as plain text
   inside the binary.  You can locate them with a hex editor, edit them in
   place (keeping the same byte length), and save the modified `PS06EPS.BIN`.

### Step 3 — Repack

After modifying `PS06EPS.BIN` (and/or `PS06UPG.BIN`):

```
# Windows
python tools\repack.py firmware_unpacked\ PS-1206U_modified.bin

# macOS / Linux
python3 tools/repack.py firmware_unpacked/ PS-1206U_modified.bin
```

The script recompresses each component into a new ZIP archive, assembles the
complete 512 KB flash image, and writes the output file.

### Step 4 — Flash

Flash the modified firmware through the web interface (works on any OS):

1. Open `http://<printer-ip>/`.
2. Navigate to **System → Upgrade**.
3. Upload `PS-1206U_modified.bin`.
4. Do **not** power-cycle during the upgrade.

> **Warning:** Flashing a corrupt or incompatible firmware image can brick the
> device.  Always keep a backup of the original `PS-1206U_v8.8.bin` before
> making any changes.
