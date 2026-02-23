# IOGear PS-1206U Print Server Firmware

Firmware version 8.8 for the IOGear PS-1206U network print server.

## Supported Printing Protocols

| Protocol | Status |
|----------|--------|
| LPR/LPD | Enabled |
| IPP (port 631) | Enabled |
| Raw TCP printing | Enabled |
| SMB/Windows printing | Enabled |
| AppleTalk/PAP | Enabled |

## AirPrint Support

The PS-1206U firmware includes a built-in IPP server (port 631, path `/ipp`).
Because the firmware does not include an mDNS/Bonjour stack, iOS and macOS
devices cannot discover the printer automatically via AirPrint without a small
amount of additional setup.

The `airprint/` directory in this repository contains an
[Avahi](https://avahi.org/) service-definition file that advertises the print
server as an AirPrint-compatible device on your local network.  Avahi is
available on any modern Linux distribution (OpenWrt routers, Raspberry Pi,
Synology/QNAP NAS appliances, etc.).

### Quick-start

1. **Assign a static IP address** to the PS-1206U (recommended: via your
   router's DHCP reservation, or through the print server's web interface at
   `http://<printer-ip>/`).

2. **Copy the service file** to the Linux system that will provide mDNS
   advertisement (it must be on the same network as the print server):

   ```bash
   sudo cp airprint/IOGear-PS1206U.service /etc/avahi/services/
   ```

3. **Edit the file** and replace every occurrence of `PRINTER_IP_ADDRESS`
   with the actual IP address of your PS-1206U:

   ```bash
   sudo nano /etc/avahi/services/IOGear-PS1206U.service
   ```

4. **Restart Avahi** to apply the change:

   ```bash
   sudo systemctl restart avahi-daemon
   ```

5. **Add the printer on your Apple device** — it should now appear as
   *"IOGear PS-1206U @ \<hostname\>"* in the AirPrint printer list.

### Requirements

- A Linux host (router, NAS, Raspberry Pi, or similar) with
  `avahi-daemon` installed and running on the same network segment as
  the PS-1206U.
- The PS-1206U must be reachable from the Linux host and from the
  Apple device on TCP port 631.

### Supported Document Formats

When printing via AirPrint the iOS/macOS client will send jobs in one
of the following formats, all of which are forwarded by the print server
to the attached USB printer:

- `application/pdf`
- `image/jpeg`
- `image/png`
- `image/urf` (Universal Raster Format)
- `application/octet-stream` (raw)

## Firmware File

| File | Description |
|------|-------------|
| `PS-1206U_v8.8.bin` | Complete firmware image (512 KB). Contains two embedded archives: the main print-server firmware (`PS06EPS.BIN`) and the bootloader/upgrade utility (`PS06UPG.BIN`). |

## Flashing the Firmware

1. Open the PS-1206U web interface at `http://<printer-ip>/`.
2. Navigate to **System → Upgrade**.
3. Upload `PS-1206U_v8.8.bin` and follow the on-screen prompts.
4. Do **not** power-cycle the device during the upgrade.

---

## Unpacking, Modifying, and Repacking the Firmware

The `tools/` directory contains two Python 3 scripts that let you extract the
firmware components, inspect or modify them, and reassemble a flashable image.

### Firmware layout

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

```bash
python3 tools/unpack.py PS-1206U_v8.8.bin firmware_unpacked/
```

This creates `firmware_unpacked/` containing:

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
   - [ndisasm](https://nasm.us/) for quick command-line disassembly:
     ```bash
     ndisasm -b 16 firmware_unpacked/PS06EPS.BIN | less
     ```

2. **Patch the binary** directly:
   - Use a hex editor (e.g. `hexedit`, `010 Editor`, `ImHex`) to change specific
     bytes, strings, or configuration values.
   - Use a Python script to automate bulk changes (e.g. replacing all occurrences
     of a hostname string).

3. **Edit embedded web pages** — the HTML/CSS/JS pages are stored as plain text
   inside the binary.  You can locate them with `strings` or a hex editor, edit
   them in place (keeping the same byte length), and save the modified
   `PS06EPS.BIN`.

### Step 3 — Repack

After modifying `PS06EPS.BIN` (and/or `PS06UPG.BIN`):

```bash
python3 tools/repack.py firmware_unpacked/ PS-1206U_modified.bin
```

The script recompresses each component into a new ZIP archive, assembles the
complete 512 KB flash image, and writes the output file.

### Step 4 — Flash

Flash the modified firmware through the web interface:

1. Open `http://<printer-ip>/`.
2. Navigate to **System → Upgrade**.
3. Upload `PS-1206U_modified.bin`.
4. Do **not** power-cycle during the upgrade.

> **Warning:** Flashing a corrupt or incompatible firmware image can brick the
> device.  Always keep a backup of the original `PS-1206U_v8.8.bin` before
> making any changes.
