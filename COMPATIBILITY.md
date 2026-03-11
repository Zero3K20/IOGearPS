# Compatibility — IOGear GPSU21 Firmware

---

## IOGear GPSU21

### About the hardware

The GPSU21 is manufactured by **ZOT Technology** (`zot.com.tw`) under OEM/ODM
model **ZOTECH PS2101-C** and uses a **MediaTek MT7628AN (MT7688)** MIPS SoC.
It is sold by IOGear under their brand.

| Field | Value |
|-------|-------|
| Brand / model | IOGear GPSU21 |
| OEM/ODM | ZOTECH PS2101-C |
| Availability | US |
| Country of manufacture | Taiwan |
| Est. release date | May 30, 2006 |
| Est. initial retail price | $49.95 USD |
| UPC | 881317005717 |
| EAN | 0881317005717 |
| Type | Print server |

#### Power

| Field | Value |
|-------|-------|
| Supply voltage | 5 VDC, 1 A |
| Connector type | Barrel plug |

#### Processing / memory

| Component | Part | Details |
|-----------|------|---------|
| CPU | MediaTek MT7628AN (MT7688) | 575–580 MHz, MIPS 24KEc |
| Flash | Winbond W25Q16JVSSIQ | 2 MB SPI NOR |
| RAM | Winbond W9725G6KB-25 | 32 MB SDRAM |

#### Networking

| Field | Value |
|-------|-------|
| Ethernet chip | MediaTek MT7628AN (integrated) |
| Switch | MediaTek MT7628AN (integrated) |
| LAN speed | 10/100 Mbps |
| LAN ports | 1 |
| Expansion interfaces | None |
| Ethernet OUI | 00:21:79 |

#### Default configuration

| Field | Value |
|-------|-------|
| IP address | 192.168.0.10 |
| Login username | admin |
| Login password | *(blank)* |
| Stock firmware OS | eCos RTOS |

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

> **TL;DR — If you downloaded a pre-built release binary from the
> [Releases](../../releases) page, the firmware is already included.
> Just flash and plug in the printer — no further steps needed.**

The CI/CD build automatically downloads the HP LJ 1020 EZ-USB firmware
blob from its long-standing public GitHub mirror and bakes it into every
release binary (Option E below).  If you build from source yourself, see
Options D and E.

**Option D — Store firmware on the print server (recommended; no PC needed
after initial setup)**

1. Obtain the HP LaserJet 1020 firmware file from one of these sources:

   | Source | File | Format | How to get |
   |--------|------|--------|------------|
   | GitHub (foo2zjs mirror) | `sihp1020.dl` | Raw binary | See script below |
   | HPLIP (Linux/macOS) | `hp_laserjet_1020.fw` | Intel HEX | `sudo apt install hplip` |
   | foo2zjs (Linux) | `sihp1020.img` | Raw binary | `sudo apt install foo2zjs` |
   | HP Windows driver | `.hex` / `.fw` | Intel HEX | Extracted from driver package |

   **Easiest — use the included download script (works on any Linux/macOS host
   with `curl` installed):**
   ```bash
   # Download the blob and upload it directly to the print server in one step:
   ./scripts/get_hp1020_firmware.sh --upload <print-server-IP>
   ```
   Or download it first and upload later:
   ```bash
   ./scripts/get_hp1020_firmware.sh --save /tmp/sihp1020.dl
   curl -X POST http://<print-server-IP>/api/upload_printer_fw \
        --data-binary @/tmp/sihp1020.dl
   ```

   The script downloads `sihp1020.dl` from:
   > `https://github.com/inveneo/hub-linux-ubuntu` (foo2zjs firmware mirror)

2. Power on the HP LaserJet 1020 with its USB cable connected to the GPSU21.
   The print server detects the stub PID, automatically performs the firmware
   upload over USB, and waits for the printer to re-enumerate.  The printer
   is then fully functional.

3. **The blob is stored in RAM only** — it is lost on print server reboot.
   Repeat step 1 after each power cycle of the print server, or use Option E
   to bake the blob permanently into the firmware image (see below).

> 💡 **Tip:** Upload the firmware blob as part of a startup script on your
> router or NAS so it is automatically restored whenever the print server
> reboots:
> ```bash
> # On router (OpenWrt example):
> ./scripts/get_hp1020_firmware.sh --save /etc/sihp1020.dl
> curl -s -X POST http://192.168.1.X/api/upload_printer_fw \
>      --data-binary @/etc/sihp1020.dl
> ```

**Option E — Bake the firmware into the print server's firmware image
(works out-of-the-box after flashing; no runtime upload needed)**

> **This is exactly what the CI build does for every release.**
> If you downloaded a pre-built release binary you can skip this section.

This option permanently embeds the HP LJ 1020 firmware into the GPSU21
firmware binary at build time.  After flashing, the print server
automatically uploads firmware to any HP LJ 1015/1020/1022 at power-on
with zero user interaction — even after a print server reboot.

```bash
# Step 1: download the firmware blob (requires curl)
./scripts/get_hp1020_firmware.sh --save /tmp/sihp1020.dl

# Step 2: build the firmware with the blob baked in
make -C firmware \
  HP1020_FW=/tmp/sihp1020.dl \
  CROSS_COMPILE=mipsel-linux-gnu- \
  FREERTOS_DIR=../freertos-kernel \
  LWIP_DIR=../lwip

# The resulting firmware/build/gpsu21_freertos.bin contains the blob.
# Flash it to the GPSU21 as normal.
```

The `HP1020_FW` build variable can point to any of the supported firmware
formats: `.dl`, `.img` (raw binary), or `.fw` / `.hex` (Intel HEX).  The
`firmware/scripts/gen_fw_blob.py` script converts it to a C header which
is compiled into `usb_printer.c`.

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
| HP LaserJet 1015 | ✅ | Needs firmware — auto via Option D/E or manual via A/B/C |
| HP LaserJet 1020 | ✅ | Needs firmware — auto via Option D/E or manual via A/B/C |
| HP LaserJet 1022 | ✅ | Needs firmware — auto via Option D/E or manual via A/B/C |
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

---

## Scanner and Fax Support (Multi-Function Devices)

### Scanners

The firmware supports two complementary scanner protocols that together cover
iOS, macOS, and Windows clients.  Both are controlled by the single
**Scanner (AirScan / WSD)** toggle in **Setup → Services**.

---

#### AirScan (eSCL) — iOS 13+ and macOS 10.15+

The firmware runs an **eSCL (AirScan) scanner server** on TCP port 9290.

When the scanner is enabled (the default), the firmware:

1. **Advertises** the scanner via mDNS as `_uscan._tcp` so that iOS and macOS
   discover it automatically — no drivers or software needed on the client.
2. **Answers eSCL HTTP requests** for scanner capabilities, scanner status, and
   scan job creation (`GET /eSCL/ScannerCapabilities`,
   `GET /eSCL/ScannerStatus`, `POST /eSCL/ScanJobs`).

---

#### WSD-Scan — Windows 7, 8, 10, 11

The firmware runs a **WS-Discovery + WSD-Scan** scanner service for Windows.
Two components work together:

| Component | Protocol | Port |
|-----------|----------|------|
| WS-Discovery responder | UDP multicast 239.255.255.250 | 3702 |
| WSD-Scan HTTP server | TCP | 5357 |

When Windows opens **Windows Scan** (or the Scanners & Cameras control panel),
it broadcasts a WS-Discovery Probe for `scan:ScannerServiceType` (Windows 8+)
or `wsdp:Device` (Windows 7).  The firmware responds with a ProbeMatch
advertising its WSD endpoint.

**Windows 7 additionally** sends a WS-Discovery **Resolve** message (addressed
to the device's `urn:uuid:` endpoint) to verify the XAddrs are still valid
before opening a TCP connection.  The firmware responds with a **ResolveMatch**
so that Windows 7 can proceed to connect to the WSD-Scan HTTP server on
port 5357.  Without this ResolveMatch, Windows 7 silently drops the device
from the scanner list.

WSD-Scan operations handled by the firmware:

| Operation | Result |
|-----------|--------|
| WS-Transfer `Get` (device metadata) | Returns device identity and scanner service endpoint |
| `GetScannerElements` | Returns scanner capabilities (modes, resolutions, formats) |
| `CreateScanJob` | Accepts the job (returns 200 OK) |
| `GetJobElements` | Returns job status |
| `RetrieveImage` | Returns SOAP fault — USB scanner driver TODO |

> **Note:** Forwarding scan data from the USB scanner to the network client
> (for both eSCL and WSD-Scan) depends on a USB scanner driver that has not
> yet been written — the same status as USB printer forwarding.  The device
> will appear in scan dialogs on all platforms, but image retrieval will fail
> until the USB scanner driver is implemented.
>
> To add USB scanner support, implement:
> - `GET /eSCL/ScanJobs/{id}/NextDocument` in `firmware/src/escl_server.c`
> - `RetrieveImage` in `firmware/src/wsd_server.c`

#### Enabling / disabling scanner support in the web interface

1. Open `http://<device-ip>/` and navigate to **Setup → Services**.
2. Find the **Scanner (AirScan / WSD)** row and set it to *Enabled* or *Disabled*.
3. Click **Save & Restart**.

#### Supported scan clients

| Client | Protocol | Support |
|--------|----------|---------|
| **iOS 13+** | AirScan (eSCL) | ✅ Native — scanner appears automatically |
| **macOS 10.15+ (Catalina+)** | AirScan (eSCL) | ✅ Native — appears in Image Capture and scan dialogs |
| **Windows 7** | WSD-Scan | ✅ Native — Probe + Resolve/ResolveMatch supported; scanner appears in Devices and Printers |
| **Windows 8 / 8.1** | WSD-Scan | ✅ Native — scanner appears in Devices and Printers |
| **Windows 10 / 11** | WSD-Scan | ✅ Native — scanner appears in Windows Scan / Devices and Printers |
| iOS 12 or earlier | — | ❌ AirScan not supported |
| macOS 10.14 (Mojave) or earlier | — | ❌ AirScan not supported |

---

### Fax machines

Standalone fax machines communicate over the **PSTN telephone network** rather
than USB, so they cannot be connected to the GPSU21 directly.

Some multi-function printers include a **USB-connected fax modem** (T.38 or
Class 1/2 fax).  The GPSU21 firmware does not currently implement any fax
forwarding protocol.  If you need network fax support, use a dedicated fax
gateway device or a software fax server on a PC.

---

## Similar and Related Devices

The IOGear GPSU21 is built on the **ZOTECH (ZOT Technology Co., Ltd.)** OEM
platform.  ZOTECH manufactures several print server models, and other consumer
brands offer functionally similar USB print servers.

### ZOTECH OEM variants (same platform as GPSU21)

The GPSU21 is a rebranded **ZOTECH PS2101-C**.  ZOTECH sells the same
underlying hardware under their own brand and supplies it to other OEM/ODM
customers.  The following ZOTECH models share the same MT7688AN SoC and
firmware generation:

| Model | Interface | Wireless | CPU | Notes |
|-------|-----------|----------|-----|-------|
| **PS2101-C** | 1× USB 2.0, 10/100 Mbps Ethernet | No | MT7688AN | The exact OEM model behind the GPSU21 |
| **PS2101W-B** | 1× USB 2.0, 10/100 Mbps Ethernet | 802.11b/g/n | MT7688AN | Wireless variant; adds Wi-Fi alongside wired Ethernet |
| **PS-2101W-A** | 1× USB 2.0, 10/100 Mbps Ethernet | 802.11b/g/n | MT7688AN | Earlier wireless variant; FCC ID `2AGB9-PS-2101W-A` |

The PS2101W-B and PS-2101W-A use the same MT7688AN SoC and the same ZOT
firmware format as the GPSU21.  The ZOT header version string prefix differs
(`J#` for 9034-series, `H#` for 9032-series), and the wireless models carry
an additional wireless driver blob.  The bootstrap and LZMA offsets may differ
from the wired GPSU21; **do not cross-flash** without first verifying the
bootstrap offset and size from a matching OEM firmware image.

ZOTECH also produces print servers based on different SoCs that are **not**
firmware-compatible with the GPSU21:

| Model | Interface | CPU | Notes |
|-------|-----------|-----|-------|
| **PAN1001B** | 1× Parallel port, 10/100 Mbps Ethernet | ARM7 | Parallel-port only; different platform |
| **PAN3001** | 3× Parallel ports, 10/100 Mbps Ethernet | ARM7 | Multi-port parallel; different platform |
| **PUN2300** | 2× USB 2.0 + 1× Parallel port, 10/100 Mbps Ethernet | RDC2886 | Multi-interface; different platform |
| **US-2101** | 1× USB 2.0, 10/100 Mbps Ethernet | E2868 | USB server (printer + storage + scanner); different platform |

### Other consumer USB print servers

The following devices from other brands offer similar single-USB-port
10/100 Mbps Ethernet print server functionality.  They are **not** based on
the ZOTECH platform and their firmware is not interchangeable with the GPSU21,
but they serve the same purpose and support the same printing protocols
(LPR/LPD, IPP, RAW TCP 9100, SMB):

| Brand / Model | USB ports | Notes |
|---------------|-----------|-------|
| **TP-Link TL-PS110U** | 1× USB 2.0 | Common budget option; web interface and protocol support similar to GPSU21 |
| **TRENDnet TE100-P1U** | 1× USB 2.0 | Single-port USB print server with similar protocol set |
| **StarTech PM1115U2** | 1× USB 2.0 | Similar form factor and feature set |
| **Edimax PS-1206U** | 1× USB 2.0 | Compact USB print server with comparable protocol support |
| **LevelOne FPS-1032** | 1× USB 2.0 | Similar single-port USB Ethernet print server |

> **Note on multi-function printer (MFP) support:** Like the GPSU21, most of
> these single-port print servers only forward raw print data and do **not**
> support the scanning or faxing functions of MFPs.  For scan support over the
> network, a device that specifically implements eSCL (AirScan) or WSD-Scan is
> required — refer to the [Scanner and Fax Support](#scanner-and-fax-support-multi-function-devices)
> section for the extended firmware capabilities of the GPSU21.

### Identifying a potential ZOTECH-platform device

If you have a USB print server of unknown origin and want to determine whether
it shares the ZOTECH MT7688 platform with the GPSU21, check for the following:

1. **Default IP address** — `192.168.0.10`
2. **Web interface appearance** — the ZOTECH/ZOT web UI has a distinctive
   tabbed layout with a **Setup** menu containing TCP/IP, Services, SMB, and
   System sub-pages.
3. **Firmware file name pattern** — ZOT firmware ZIPs contain a single `.bin`
   file whose name follows the pattern `MPS56_<variant>_<build>_<date>.bin`.
4. **ZOT header magic** — the first four bytes of the `.bin` file are the ZOT
   header signature; offset 0x28 contains a version string of the form
   `MT7688-<version>-<date>`.
5. **uImage name** — the embedded uImage header names the kernel `zot716u2`.

If all five match, the device almost certainly runs the ZOT MT7688 firmware
and the tools and patching approach in this repository will apply.
