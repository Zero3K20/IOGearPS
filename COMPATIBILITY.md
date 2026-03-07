# Compatibility — IOGear GPSU21 Firmware

---

## IOGear GPSU21

### About the hardware

The GPSU21 is manufactured by **ZOT Technology** (`zot.com.tw`) and uses a
**MediaTek MT7688** MIPS SoC.  It is sold by IOGear under their brand.

Internal device identifiers:
- OEM model path: `pu211`
- uImage name: `zot716u2`
- CPU: `MT7688`
- RTOS: **eCos** (confirmed by `cyg_flash_erase` and `ecosif_input` symbols in binary)

### Firmware in this repository

`MPS56_90956F_9034_20191119.zip` contains the official ZOT firmware for the
GPSU21:

| Field | Value |
|-------|-------|
| Version | 9.09.56 (variant F) |
| Build | 9034 |
| Build date | 2019/11/19 13:00:10 |
| File inside ZIP | `MPS56_90956F_9034_20191119.bin` |
| Uncompressed size | 350,428 bytes |
| Format | 256-byte ZOT header + uImage header + LZMA-compressed eCos binary |
| CPU architecture | MIPS (MediaTek MT7688) |

> Earlier builds of this same 9.09.56 series also exist (e.g. build 9032
> from 2017/11/23).  The firmware in this repository is the **newer** build
> 9034 from 2019.

### Firmware version format

The version string is embedded in the firmware at offset 0x28:

```
MT7688-<major>.<minor>.<release>.<build>.<serial>-<date> <time>
```

Example from this firmware:

```
MT7688-9.09.56.9034.00001243t-2019/11/19 13:00:10
```

### Extracting and modifying the web interface

The GPSU21 firmware is a single LZMA-compressed eCos binary.  All web
interface files — HTML pages, JavaScript, CSS, and JPEG/GIF images — are
stored **uncompressed inside that binary** and can be extracted, edited, and
repacked using the tools in this repository.

```
# Windows (pass the .zip directly):
python tools\unpack_gpsu21.py  MPS56_90956F_9034_20191119.zip  gpsu21_extracted\

# macOS / Linux:
python3 tools/unpack_gpsu21.py  MPS56_90956F_9034_20191119.zip  gpsu21_extracted/
```

This produces a directory containing 61 files:

| Category | Files | Examples |
|----------|-------|---------|
| HTML pages | 28 | `IPP.HTM`, `UPGRADE.HTM`, `SYSTEM.HTM`, `TCPIP.HTM` |
| JavaScript | 11 | `status.js`, `setup.js`, `navigator.htm` |
| CSS | 1 | `basic_style.css` |
| JPEG images | 15 | `images/InfoImg.jpg`, `images/MenuBtn-CS-*.jpg` |
| GIF images | 2 | `images/disabled2.gif`, `images/enabled2.gif` |

Edit any file, then repack and flash:

```
# Windows:
python tools\repack_gpsu21.py  MPS56_90956F_9034_20191119.zip  gpsu21_extracted\  gpsu21_modified.bin

# macOS / Linux:
python3 tools/repack_gpsu21.py  MPS56_90956F_9034_20191119.zip  gpsu21_extracted/  gpsu21_modified.bin
```

> **Size constraint:** each edited file must be the **same size or smaller**
> than the original.  If you need to shorten a file, pad it to the original
> size with spaces or HTML comments.  See `tools/repack_gpsu21.py` for details.

### Flashing the GPSU21

The GPSU21 web interface accepts firmware via its upgrade page.  Extract the
`.bin` file from the ZIP before uploading:

1. Open `http://<printer-ip>/` in a browser.
2. Navigate to the firmware upgrade page.
3. Upload `MPS56_90956F_9034_20191119.bin` (not the ZIP).
4. Wait for the upgrade to complete.
5. **Do not power-cycle** during the upgrade.

### Source

The firmware ZIP was originally hosted at:

```
https://www.zot.com.tw/zot-file/pu211/MPS56_90956F_9034_20191119.zip
```

---

## Printer Compatibility

### Overview

The GPSU21 print server supports **any USB printer that implements the USB
Printer Class** (USB Class 7, Subclass 1) with either:

- **Protocol 1** — Unidirectional (data flows host → printer only)
- **Protocol 2** — Bi-directional (data flows both ways; enables live status
  back-channel)

When a printer is connected, the firmware reads its **IEEE 1284 Device ID**
string and uses it to populate the web interface status page.

### Standard printers — known to work

Any printer that:

1. Uses a standard page description language such as **PCL**, **PostScript**,
   or **PDF**, **and**
2. Implements USB Printer Class (Class 7, Protocol 1 or Protocol 2)

…will work with this print server.  The server forwards raw print data from the
network client to the printer without modification.

Examples of known-compatible categories:

| Category | Examples |
|----------|---------|
| HP LaserJet (PCL) | LaserJet 4, 4050, 4100, 4200, P2015, P3010 series |
| HP DeskJet/OfficeJet (PCL) | Most post-2005 models |
| HP Color LaserJet (PCL+PS) | CP series, Enterprise series |
| Brother Laser (PCL/PS) | HL, DCP, MFC series |
| Canon laser (UFR II / PCL) | LBP, MF series |
| Epson (ESC/P, ESC/Page) | Most post-2000 models |
| Xerox/Fujifilm (PS/PCL) | Phaser, WorkCentre series |
| Kyocera (PCL/PS/KPDL) | FS, ECOSYS series |
| OKI (PCL/PS) | B, C series |

### Host-based (GDI / WinPrint) printers — require special setup

Host-based printers do **not** contain their own rendering engine.  Instead,
the host computer generates a proprietary raster stream using a vendor driver,
and the printer just prints the pixel data.  They typically work with a print
server **provided the correct driver is installed on the client PC** and the
driver is configured to produce the appropriate raster format.

| Printer family | Protocol | Works with this print server? |
|----------------|----------|-------------------------------|
| HP LaserJet 1000 series (non-firmware) | ZjStream | ✅ Yes — see below |
| HP LaserJet P1xxx (e.g. P1005, P1006) | CAPT / ZjStream | ✅ Yes — see below |
| Samsung ML-1xxx/SCX-3xxx | SPL | ✅ Yes — driver must be installed on client |
| Lexmark Z-series / E series | PCL-XL | ✅ Yes — standard PCL driver |
| Canon PIXMA (home inkjet) | BJNP | ⚠️ Mostly yes — requires Canon IJ driver |

### HP LaserJet 1020 (and 1015, 1022) — firmware-free printers

The **HP LaserJet 1015**, **1020**, and **1022** are a special case.  These
printers store their operating firmware in RAM: every time the printer is
powered on, the host computer must upload the firmware over USB before the
printer will operate.  Without the firmware, the printer enumerates as a
vendor-specific USB device (not a Printer Class device) and cannot accept print
data.

#### The upload protocol — clean-room reverse engineering

The **upload protocol** used to transfer firmware to these printers is the
**Cypress EZ-USB ANCHOR_LOAD_INTERNAL** vendor request.  This has been
**fully reverse-engineered** by the open-source community and is now
documented and implemented in multiple independent open-source projects:

| Project | File(s) |
|---------|---------|
| Linux kernel | `drivers/usb/misc/ezusb.c` — `ezusb_ihex_firmware_download()` |
| foo2zjs | `usb/foo2usb-wrapper`, `firmware/Makefile` |
| HPLIP | `base/firmware.py` — `__load_firmware()` |

The protocol is a standard USB vendor control request:

```
bmRequestType = 0x40  (OUT | VENDOR | DEVICE)
bRequest      = 0xA0  (ANCHOR_LOAD_INTERNAL)
wValue        = target address in EZ-USB internal 8051 RAM
wIndex        = 0
Data          = firmware bytes to write at wValue
```

Sequence:
1. Write `{0x01}` to address `0xE600` (CPUCS) — hold 8051 CPU in reset
2. Write firmware bytes in chunks to their respective load addresses
3. Write `{0x00}` to address `0xE600` — release CPU; firmware boots and
   the device re-enumerates on the USB bus with its operational PID

**This firmware now implements this protocol natively** (see `usb_fw_upload.c`
in the source).  The firmware upload can be performed autonomously by the
print server — no PC required at print time — as long as the firmware binary
has been uploaded once via the web interface (see Option D below).

#### What is NOT reverse-engineered

The **firmware binary itself** (the code that runs on the HP LaserJet 1020's
8051 CPU) is HP-proprietary and cannot be redistributed.  The upload protocol
code in this repository implements the *transport mechanism* only; the binary
payload must be obtained from HP or from the HPLIP package.

A fully open-source, clean-room replacement for the HP LaserJet 1020's
internal firmware (rendering engine, ZjStream PCL-XL interpreter, etc.) does
not currently exist and would be an entirely separate, much larger project.

#### USB device identifiers

| State | USB Vendor:Product | Notes |
|-------|--------------------|-------|
| Power-on (no firmware) | `03f0:2911` (HP LJ 1015) | Stub — firmware not loaded |
| Power-on (no firmware) | `03f0:2b17` (HP LJ 1020) | Stub — firmware not loaded |
| Power-on (no firmware) | `03f0:2c17` (HP LJ 1022) | Stub — firmware not loaded |
| Ready (firmware loaded) | `03f0:3315` (HP LJ 1015) | USB Printer Class device |
| Ready (firmware loaded) | `03f0:3417` (HP LJ 1020) | USB Printer Class device |
| Ready (firmware loaded) | `03f0:3517` (HP LJ 1022) | USB Printer Class device |

#### What the firmware does when a pre-firmware device is detected

When the GPSU21 detects a stub-PID HP LaserJet, it first checks whether a
firmware blob has been stored:

- **Blob stored** (Option D below): the print server automatically performs
  the ANCHOR_LOAD_INTERNAL upload sequence, waits for the device to
  re-enumerate, and then proceeds to enumerate it as a standard USB Printer
  Class device.  **No PC or manual intervention is needed.**

- **No blob stored**: the `needs_firmware` flag is set, print jobs are
  rejected, a message is logged to the serial console, and the
  `/api/printer_status` JSON endpoint reports `"needs_firmware": true`.

The `needs_firmware` flag is cleared automatically when the USB device is
physically disconnected.

#### How to make the HP LaserJet 1020 work with this print server

**Option D — Store firmware on the print server (recommended; no PC needed
after initial setup)**

1. Obtain the HP LaserJet 1020 firmware file from one of these sources:

   | Source | File | Format |
   |--------|------|--------|
   | HPLIP (Linux/macOS) | `/usr/share/hplip/data/firmware/hp_laserjet_1020.fw` | Intel HEX |
   | foo2zjs (Linux) | extracted `sihp1020.img` (see below) | Raw binary |
   | HP Windows driver | extracted from `.inf` / `.hex` | Intel HEX or raw |

   **Easiest: extract from HPLIP** (the file is freely downloadable from HP
   via the HPLIP package but is not redistributable):
   ```bash
   sudo apt install hplip          # Debian/Ubuntu
   # or: sudo dnf install hplip   # Fedora/RHEL
   ls /usr/share/hplip/data/firmware/hp_laserjet_1020.fw
   ```

2. Upload the firmware file to the print server:
   ```bash
   curl -X POST http://<print-server-IP>/api/upload_printer_fw \
        --data-binary @/usr/share/hplip/data/firmware/hp_laserjet_1020.fw
   ```
   Expected response: `{"ok":true,"bytes":49152}` (size varies by firmware
   version).

3. Power on the HP LaserJet 1020 with its USB cable connected to the GPSU21.
   The print server detects the stub PID, automatically performs the firmware
   upload over USB, and waits for the printer to re-enumerate.  The printer
   is then fully functional.

4. **The blob is stored in RAM only** — it is lost on print server reboot.
   Repeat step 2 after each power cycle of the print server, or connect the
   printer to HPLIP/Windows first then reconnect (Options A/B below).

> 💡 **Tip:** Upload the firmware blob as part of a startup script on your
> router or NAS so it is automatically restored whenever the print server
> reboots:
> ```bash
> # On router (OpenWrt example):
> curl -s -X POST http://192.168.1.X/api/upload_printer_fw \
>      --data-binary @/etc/hplip/hp_laserjet_1020.fw
> ```

**Option A — Pre-load on a Windows PC (easiest, no blob needed)**

1. Install the official HP LaserJet 1020 driver on a Windows PC.
2. Connect the printer to the Windows PC via USB and power it on.
3. Wait ~10 seconds for Windows to upload the firmware automatically.
4. Disconnect the USB cable from the PC.
5. Connect the USB cable to the GPSU21 print server USB port.
6. The printer now appears as `03f0:3417` (Printer Class) and is fully
   functional.

> ⚠️ Do **not** power-cycle the printer after moving it — it will lose the
> firmware.

**Option B — Pre-load from a Linux/macOS host**

1. Install `hplip`:
   - Debian/Ubuntu: `sudo apt install hplip`
   - Fedora/RHEL: `sudo dnf install hplip`
   - Arch Linux: `sudo pacman -S hplip`
   - macOS (Homebrew): `brew install hplip`
2. Connect the printer to the Linux/macOS host and power it on.
3. HPLIP uploads the firmware automatically via `hp-firmware` or the udev
   rules installed with HPLIP.
4. Confirm the printer is recognised (`lsusb` on Linux or
   `system_profiler SPUSBDataType` on macOS shows the printer at `03f0:3417`).
5. Disconnect from the Linux/macOS host and connect to the GPSU21.

**Option C — Use `foo2zjs` on Linux**

```bash
# Install foo2zjs (may need to compile from source on modern distros)
sudo apt install foo2zjs

# After connecting printer:
sudo /usr/share/foo2zjs/usb/foo2usb-wrapper

# Confirm firmware loaded:
lsusb | grep "03f0:3417"
```

#### Client driver configuration for printing via the print server

After the printer has firmware, install the HP LaserJet 1020 driver on each
client PC and configure the printer to print to the GPSU21 using one of the
supported protocols:

| Protocol | Port / address |
|----------|---------------|
| Raw TCP (JetDirect) | `<print-server-IP>:9100` |
| LPR/LPD | `lpr://<print-server-IP>/lp1` |
| IPP | `ipp://<print-server-IP>:631/printers/lp1` |

On **Linux/macOS with CUPS**:

```bash
# Add the printer via CUPS, specifying the correct driver and raw TCP socket:
lpadmin -p HPLaserJet1020 \
        -E \
        -v socket://<print-server-IP>:9100 \
        -m drv:///hp/hpcups.drv/hp-laserjet_1020.ppd
```

On **Windows**: use the HP LaserJet 1020 driver, select "Standard TCP/IP Port",
enter `<print-server-IP>`, port `9100`.

### Summary table

| Printer | Works? | Notes |
|---------|--------|-------|
| HP LaserJet PCL (1990s–present) | ✅ | Works out-of-the-box |
| HP LaserJet Pro / Enterprise | ✅ | Works out-of-the-box |
| HP LaserJet 1015 | ✅ | Needs firmware — auto-upload via Option D or manual via A/B/C |
| HP LaserJet 1020 | ✅ | Needs firmware — auto-upload via Option D or manual via A/B/C |
| HP LaserJet 1022 | ✅ | Needs firmware — auto-upload via Option D or manual via A/B/C |
| HP DeskJet / OfficeJet / Envy (USB) | ✅ | Works with correct PCL driver on client |
| Brother HL / DCP / MFC | ✅ | Works out-of-the-box |
| Epson inkjet (ESC/P) | ✅ | Works with correct driver on client |
| Canon PIXMA / MAXIFY | ✅ | Works; Canon IJ driver or Gutenprint recommended |
| Samsung/Xerox monochrome laser | ✅ | Works with PCL driver or vendor driver on client |
| PostScript printers | ✅ | Works out-of-the-box |



The GPSU21 firmware ships with a **built-in Bonjour (mDNS) stack** and an
**IPP server on port 631**.  It automatically advertises itself as `_ipp._tcp`
on the local network — no PC software or additional scripts are required for
modern Apple devices.

### Device compatibility

| Apple device | Support |
|---|---|
| **iOS 14+** | ✅ Native — print server appears automatically |
| **macOS 11+ (Big Sur and later)** | ✅ Native — print server appears automatically |
| iOS 13 or earlier | ⚠️ Needs optional Avahi/Bonjour helper |
| macOS 10.15 (Catalina) or earlier | ⚠️ Needs optional Avahi/Bonjour helper |

### Enabling AirPrint in the web interface

After flashing the modified firmware:

1. Open `http://<printer-ip>/` and navigate to **Setup → TCP/IP**.
2. Under *Rendezvous (Bonjour) Settings*, set **Service** to *Enable* and enter
   a **Service Name** (e.g. `IOGear GPSU21`).
3. Navigate to **Setup → Services** and ensure **Use IPP** is set to *Enabled*.
4. Click **Save & Restart**.

The printer will appear automatically in the AirPrint list on iOS 14+ and
macOS 11+ devices on the same network.

### Optional helpers for older Apple devices

If you also need to support iOS 13 / macOS 10.15 or earlier, you can manually
advertise the `_universal._sub._ipp._tcp` sub-type using an Avahi service file
on Linux or a Bonjour helper script on Windows.

> **Installing software on your PC is NOT required for iOS 14+ / macOS 11+.**
