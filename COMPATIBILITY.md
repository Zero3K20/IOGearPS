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
**IPP server on port 631**.  It advertises itself as `_ipp._tcp` on the local
network.

### Device compatibility

| Apple device | Support |
|---|---|
| **iOS 14 – iOS 15** | ✅ Native — print server appears automatically |
| **macOS 11 (Big Sur) – macOS 12 (Monterey)** | ✅ Native — print server appears automatically |
| **iOS 16+ (including iOS 26.x)** | ⚠️ Needs `_universal._sub._ipp._tcp` helper — see below |
| **macOS 13 (Ventura) and later** | ⚠️ Needs `_universal._sub._ipp._tcp` helper — see below |
| iOS 13 or earlier | ⚠️ Needs `_universal._sub._ipp._tcp` helper — see below |
| macOS 10.15 (Catalina) or earlier | ⚠️ Needs `_universal._sub._ipp._tcp` helper — see below |

> **Why iOS 16+ also needs the helper:** Starting with iOS 16, Apple tightened
> AirPrint discovery.  Devices running iOS 16 or later (including iOS 26.x)
> require the printer to be advertised with the `_universal._sub._ipp._tcp`
> Bonjour sub-type.  The GPSU21 firmware advertises only the base `_ipp._tcp`
> service, so iOS 16+ devices will report "No AirPrint Printers Found" unless
> the sub-type is re-advertised by a helper on the same network.

### Enabling AirPrint in the web interface

After flashing the modified firmware:

1. Open `http://<printer-ip>/` and navigate to **Setup → TCP/IP**.
2. Under *Rendezvous (Bonjour) Settings*, set **Service** to *Enable* and enter
   a **Service Name** (e.g. `IOGear GPSU21`).
3. Navigate to **Setup → Services** and ensure **Use IPP** is set to *Enabled*.
4. Click **Save & Restart**.

The printer will appear automatically in the AirPrint list on **iOS 14–15** and
**macOS 11–12** devices on the same network.  For iOS 16+ (including iOS 26.x)
and macOS 13+, also follow the steps in the next section.

### Required helper for iOS 16+ / iOS 26.x and macOS 13+

iOS 16 and later — including all iOS 26.x releases — require the printer to be
advertised with the `_universal._sub._ipp._tcp` Bonjour sub-type in addition to
the base `_ipp._tcp` service.  The same requirement applies to macOS 13 (Ventura)
and later.

You can satisfy this requirement without modifying the printer firmware by
running a small Bonjour proxy on any always-on device on the same network:

**Linux (Avahi)**

Create `/etc/avahi/services/airprint.service` with the following content,
replacing `<printer-ip>` and `<service-name>` with your actual values:

```xml
<?xml version="1.0" standalone='no'?>
<!DOCTYPE service-group SYSTEM "avahi-service.dtd">
<service-group>
  <name replace-wildcards="yes">%h AirPrint</name>
  <service>
    <type>_ipp._tcp</type>
    <subtype>_universal._sub._ipp._tcp</subtype>
    <port>631</port>
    <host-name><printer-ip></host-name>
    <txt-record>txtvers=1</txt-record>
    <txt-record>qtotal=1</txt-record>
    <txt-record>rp=ipp/print</txt-record>
    <txt-record>ty=<service-name></txt-record>
    <txt-record>adminurl=http://<printer-ip>/</txt-record>
    <txt-record>pdl=application/octet-stream,application/pdf,image/jpeg,image/png</txt-record>
    <txt-record>URF=none</txt-record>
  </service>
</service-group>
```

Then reload Avahi:

```bash
sudo systemctl reload avahi-daemon
```

**Windows / macOS (dns-sd)**

```
dns-sd -R "IOGear GPSU21" _ipp._tcp,_universal._sub._ipp._tcp local 631 \
  txtvers=1 qtotal=1 rp=ipp/print ty="IOGear GPSU21" \
  adminurl=http://<printer-ip>/ \
  pdl=application/octet-stream,application/pdf,image/jpeg,image/png \
  URF=none
```

Once the helper is running, iOS 16+ / iOS 26.x and macOS 13+ devices on the
same network will discover the printer through AirPrint.

> **Note:** The helper only advertises the printer — all print jobs still go
> directly from the Apple device to the GPSU21 at `<printer-ip>:631`.
