# IOGear GPSU21 / Edimax PS-1206U Firmware Repository

This repository contains the firmware for the **IOGear GPSU21** USB Print
Server (also sold as the **Edimax PS-1206U**), together with a helper script
for creating a firmware backup directly from a live device.

## Included files

| File | Description |
|------|-------------|
| `PS-1206U_v8.8.bin` | Factory firmware image (version 8.8) |
| `backup_firmware.py` | Script to back up firmware from a running device |

---

## Backing up the firmware on your IOGear GPSU21

### Why back up?

Before upgrading the firmware it is good practice to keep a copy of the
version currently running on your device so that you can restore it if
anything goes wrong.

### Prerequisites

* Python 3.7 or later (no third-party packages required — only the standard
  library is used).
* The print server must be reachable on your local network.

### Usage

```bash
python backup_firmware.py --host <device-ip> [options]
```

**Required argument**

| Argument | Description |
|----------|-------------|
| `--host IP_OR_HOSTNAME` | IP address or hostname of the print server |

**Optional arguments**

| Argument | Default | Description |
|----------|---------|-------------|
| `--output FILE` | `firmware_backup.bin` | Destination file for the backup |
| `--username USER` | `admin` | Web-interface login username |
| `--password PASS` | *(empty)* | Web-interface login password |
| `--reference FILE` | `PS-1206U_v8.8.bin` | Reference image to compare the backup against |

### Example

```bash
# Basic usage — save backup to firmware_backup.bin
python backup_firmware.py --host 192.168.0.1

# Custom output path and credentials
python backup_firmware.py --host 192.168.0.1 \
    --output my_gpsu21_backup.bin \
    --username admin \
    --password mysecretpassword
```

The script will:

1. Connect to the print server web interface.
2. Search for the firmware image at known locations.
3. Save the image to the specified output file.
4. Print the SHA-256 checksum of the saved image.
5. Optionally compare the backup against the bundled `PS-1206U_v8.8.bin`
   reference image.

### Finding your device's IP address

If you do not know the IP address of your print server, you can:

* Check your router's DHCP client list for a device named **PS-1206U** or
  with an Edimax / IOGear MAC prefix.
* Use the **BiAdmin** utility (included in the CD that shipped with the
  device) — it scans the local network and lists detected print servers.

### Restoring the firmware

To restore a previously saved firmware image (or to flash the bundled
`PS-1206U_v8.8.bin`):

1. Open a browser and navigate to `http://<device-ip>/`.
2. Log in with your administrator credentials.
3. Go to **Administration → Firmware Upgrade**.
4. Browse to the `.bin` file you want to restore and click **Upgrade**.

> **Note:** Do not power off the device during a firmware upgrade. A
> failed upgrade can render the device unresponsive until it is re-flashed
> via the web interface or via the emergency-recovery mode described in the
> device manual.

---

## Hardware-level firmware extraction (advanced)

If the device is unresponsive and cannot be reached via the network, the
only remaining option is to extract the firmware directly from the flash
chip on the PCB using a hardware programmer (e.g. CH341A) and a SOIC-8
clip.  This is an advanced procedure that requires soldering skills and
appropriate equipment; consult embedded-systems forums for guidance.

---

## License

The firmware binary (`PS-1206U_v8.8.bin`) is © Edimax Technology Co., Ltd.
and is redistributed here for archival purposes.  The `backup_firmware.py`
script is released under the [MIT License](https://opensource.org/licenses/MIT).
