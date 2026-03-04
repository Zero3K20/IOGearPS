# Compatibility — IOGear GPSU21 and PS-1206U Firmware

> **Important:** The IOGear PS-1206U and the IOGear GPSU21 use **completely
> different hardware** and their firmware images are **not interchangeable**.
> See the hardware table below before attempting to flash any firmware.

---

## Hardware comparison

| | IOGear PS-1206U | IOGear GPSU21 |
|---|---|---|
| **OEM manufacturer** | Edimax Technology | ZOT Technology (`zot.com.tw`) |
| **OEM model code** | PS-1206U (`Edimax8820`) | `pu211` / `zot716u2` |
| **CPU** | Edimax 8820 (x86 real-mode) | MediaTek MT7688 (MIPS 24KEc) |
| **Firmware format** | 512 KB flat flash image (Edimax header + ZIP archives) | 350 KB uImage (ZOT header + uImage + compressed payload) |
| **Firmware in this repo** | `PS-1206U_v8.8.bin` | `MPS56_90956F_9034_20191119.zip` |
| **Cross-flashable?** | ❌ **No** | ❌ **No** |

**Do not attempt to flash PS-1206U firmware onto a GPSU21, or vice versa.**
The two devices have incompatible CPUs, flash memory sizes, and firmware
formats.  Doing so will render the device unbootable and require physical
flash chip reprogramming to recover.

---

## IOGear PS-1206U

### Firmware in this repository

`PS-1206U_v8.8.bin` — version 8.8, an older release of the Edimax print server
firmware.

### Firmware version format

The version is displayed on the device's web **Status** page:

```
<major>.<minor>.<release><suffix> <build> (<date> <time>)
```

Example: `8.8.xx` or a version in that older series.

### Upgrade validation

The PS-1206U web interface validates the uploaded file before writing to flash:

1. **File size** — must be exactly 512 KB (524,288 bytes).
2. **Magic bytes** — the first 6 bytes must be `45 01 FA EB 13 90`.
3. **MTYPE label** — the model tag (`Edimax8820 MTYPE/`) must match.

The validator does **not** check the firmware version number.  If any check
fails, the page shows an error and does **not** touch the flash:

| Error page | Cause |
|------------|-------|
| "Invalid size" | File is not exactly 512 KB |
| "Signature wrong" | Magic bytes or MTYPE tag do not match |
| "Upgrade Failed" | Flash write error (hardware fault) |

### Flashing procedure

1. Open `http://<printer-ip>/` and log in (default password: `1234`).
2. Navigate to **System → Upgrade**.
3. Click **Browse**, select `PS-1206U_v8.8.bin`, then click **Next / OK**.
4. Wait for the "Upgrade successfully!" message.
5. **Do not power-cycle** during the upgrade.

---

## IOGear GPSU21

### About the hardware

The GPSU21 is manufactured by **ZOT Technology** (`zot.com.tw`) and uses a
**MediaTek MT7688** MIPS SoC.  It is sold by IOGear under their brand but is
completely different hardware from the PS-1206U.

Internal device identifiers:
- OEM model path: `pu211`
- uImage name: `zot716u2`
- CPU: `MT7688`

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
| Format | 256-byte ZOT header + uImage header + compressed payload |
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

The GPSU21 runs a Linux-based firmware with an IPP server.  To enable AirPrint
discovery, use the same mDNS advertisement approach as the PS-1206U:

- **Windows:** run `airprint\windows-bonjour.bat` (edit the IP address first)
- **Linux:** deploy `airprint/IOGear-PS1206U.service` with Avahi

See [README.md](README.md#airprint-support) for the full setup guide.
