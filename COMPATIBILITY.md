# Compatibility — IOGear GPSU21 and PS-1206U Firmware

This document explains the relationship between the **IOGear PS-1206U** and the
**IOGear GPSU21** print servers, and what to expect when flashing the
`PS-1206U_v8.8.bin` firmware.

---

## Firmware versions

### Version in this repository: v8.8

The firmware image in this repository (`PS-1206U_v8.8.bin`) is **version 8.8**,
an older release of the Edimax/IOGear print server firmware.

### Finding your device's current version

The firmware version is displayed on the **Status** page of the device web
interface (typically the first page after logging in).  The version string
follows the format:

```
<major>.<minor>.<release><suffix> <build> (<date> <time>)
```

For example:

```
9.09.56I 9032 (2017/11/23 10:36:02)
```

means:

| Field | Value | Meaning |
|-------|-------|---------|
| Major | `9` | Major version |
| Minor | `09` | Minor version |
| Release | `56` | Release build |
| Suffix | `I` | Internal build letter |
| Build | `9032` | Build number |
| Date | `2017/11/23` | Build date |

### What if my device runs a newer version than v8.8?

If your device shows a version **higher than 8.8** (for example `9.09.56I` from
2017), flashing `PS-1206U_v8.8.bin` would be a **firmware downgrade**.

Key points about downgrading:

- The firmware validator **does not check the version number** — it only checks
  the file size, magic bytes, and MTYPE model tag (see below).  So the upgrade
  page will *accept* the older firmware without error.
- After flashing, the device will run the older v8.8 firmware.  All features
  added in later firmware releases (between v8.8 and your current version)
  will no longer be available.
- Saved configuration (device name, IP settings, passwords, port names) is
  stored in a separate NVRAM area and may survive the downgrade, but this is
  not guaranteed.  Reset the device to factory defaults after flashing to be
  safe.
- **Downgrading is generally not recommended** unless you have a specific
  reason (e.g. a known regression in the newer firmware that you want to
  avoid).  If your device is working normally on v9.09, there may be no
  benefit to flashing v8.8.

> **Recommendation for v9.09 users:** If you only need AirPrint support and
> your device is otherwise working, consider using the mDNS advertisement
> approach from [README.md](README.md#airprint-support) *without* flashing
> new firmware.  Your device's existing IPP server (port 631) already
> supports AirPrint jobs.

---

## Hardware relationship

Both the **PS-1206U** and the **GPSU21** are single-port USB print servers sold
by IOGear.  IOGear print servers are manufactured by (or jointly with)
**Edimax Technology**, and the firmware image carries two internal model tags:

| Tag | Value |
|-----|-------|
| Main firmware header | `Edimax8820 MTYPE/` |
| Upgrade utility header | `Edimax7420 MTYPE/` |

The PS-1206U and the GPSU21 share the same Edimax 8820 chipset and PCB design.
In practice, users have reported successfully flashing PS-1206U firmware onto
GPSU21 hardware (and vice-versa), because the device internally identifies
itself as `Edimax8820`.

---

## Is it safe to try?

**Short answer: yes, trying via the web interface is safe.**

The firmware upgrade page runs an in-firmware validation before writing
anything to flash.  The validator checks:

1. **File size** — must be exactly 512 KB (524,288 bytes).
2. **Magic bytes** — the first 6 bytes must be `45 01 FA EB 13 90`.
3. **MTYPE label** — the model tag embedded in the header
   (e.g. `Edimax8820 MTYPE/`) must match the tag the device was manufactured
   with.

The validator does **not** check the firmware version number, so both
upgrades (older → newer) and downgrades (newer → older) will pass as long as
the MTYPE label matches.

If any of the three checks fail, the web interface shows one of these error pages
and **does not touch the flash**:

| Error page | Cause |
|------------|-------|
| "Invalid size" | File is not exactly 512 KB |
| "Signature wrong" | Magic bytes or MTYPE tag do not match |
| "Upgrade Failed" | Flash write error (hardware fault) |

In other words, if this firmware is incompatible with your GPSU21, the upgrade
will be **rejected cleanly** — the device will keep running its current
firmware and will not be bricked.

> **The only way to brick the device** would be to physically reprogram the
> flash chip with a programmer, or to interrupt power during an otherwise
> successful flash write.  Neither of those can happen through the web
> interface.

---

## How to check your GPSU21's model tag

If you want to verify compatibility before uploading, you can read the current
firmware model tag from the GPSU21 web interface:

1. Open `http://<printer-ip>/` in a browser.
2. Navigate to the **Status** page (usually the default home page).
3. Note the firmware version string shown on the page (e.g. `PS1206U v8.8`).

Alternatively, download the official GPSU21 firmware from IOGear's support
site and run the unpack tool:

```
python tools\unpack.py <GPSU21-firmware>.bin gpsu21_unpacked\
```

Look at `gpsu21_unpacked\layout.txt` — if it shows `Edimax8820 MTYPE/` for
Header 1, the firmware images are interchangeable.

---

## Flashing procedure (any OS)

The web-based upgrade page works in any modern browser on Windows, macOS, or
Linux — no special software is needed:

1. Download or build the firmware `.bin` file.
2. Open `http://<printer-ip>/` and log in (default password: `1234`).
3. Navigate to **System → Upgrade**.
4. Click **Browse**, select the `.bin` file, then click **Next / OK**.
5. Wait for the progress bar and the "Upgrade successfully!" message.
6. **Do not power-cycle** the device while the upgrade is in progress.

---

## Known compatible models

The following IOGear / Edimax models share the `Edimax8820` platform and are
expected to be firmware-compatible:

| Model | OEM Brand | Notes |
|-------|-----------|-------|
| PS-1206U | IOGear | Primary firmware target |
| GPSU21 | IOGear | Same chipset; verified compatible by users |
| PS-1206MF | IOGear | Multi-function variant; may require own firmware |
| PS-1206U | Edimax | OEM original |

> If you have verified that this firmware works on another model, please open
> an issue or pull request to add it to this table.
