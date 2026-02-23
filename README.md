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
