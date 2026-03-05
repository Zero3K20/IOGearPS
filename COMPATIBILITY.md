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

## AirPrint on the GPSU21

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
