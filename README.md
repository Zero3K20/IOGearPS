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

If the device is unresponsive and cannot be reached via the network — or if
you simply want a guaranteed, bit-perfect backup of the raw flash contents —
you can read the firmware directly from the SPI NOR flash chip on the PCB
using an inexpensive USB chip programmer and a test clip.  No soldering is
required when an SOIC-8 clip is used.

### Flash chip overview

The IOGear GPSU21 / Edimax PS-1206U stores its firmware in a **512 KB SPI
NOR flash chip** in an **SOIC-8 package** (8 pins, two rows of 4) located
on the main PCB near the central processor.  The chip is commonly a
**Winbond W25X40** or **Macronix MX25L4005** (or a pin-compatible
equivalent); the actual part number is silk-screened on the chip's surface.

### Required hardware

| Item | Notes |
|------|-------|
| **CH341A USB programmer** | ~$5 on most electronics marketplaces; comes in a black or green PCB form factor |
| **SOIC-8 test clip** (e.g. Pomona 5250 or generic) | Grips the chip in-circuit — no desoldering needed |
| **Jumper wires** (if clip lacks a DuPont connector) | Connect clip to programmer header |

> **Voltage warning:** The CH341A outputs 5 V on VCC by default on many
> clones.  The flash chip on this device operates at **3.3 V**.  Either
> use a programmer that supports a 3.3 V mode / has a 3.3 V jumper, or add
> a small 3.3 V LDO regulator between the programmer's VCC pin and the clip's
> VCC wire (connect both the VCC and GND lines of the LDO — GND is typically
> shared between the programmer and the clip).  Applying 5 V to a 3.3 V chip
> can damage it permanently.

### SOIC-8 pinout and wiring

Standard 25xx SPI NOR flash chips follow this pinout (pin 1 is indicated by
a dot or chamfer on the chip body):

```
        ┌──────────┐
  CS  1 │●         │ 8  VCC
MISO  2 │          │ 7  HOLD#
  WP  3 │          │ 6  CLK
 GND  4 │          │ 5  MOSI
        └──────────┘
```

Connect the SOIC-8 clip to the CH341A programmer as follows:

| Chip pin | Signal | CH341A label |
|----------|--------|--------------|
| 1 | CS / CE | CS |
| 2 | MISO / DO | MISO (or DO) |
| 3 | WP# | tie to VCC (3.3 V) |
| 4 | GND | GND |
| 5 | MOSI / DI | MOSI (or DI) |
| 6 | CLK / SCK | CLK |
| 7 | HOLD# | tie to VCC (3.3 V) |
| 8 | VCC | VCC (3.3 V) |

Pins 3 (WP#) and 7 (HOLD#) must be held high (connected to 3.3 V) to keep
the chip writable and always-selected.  Many CH341A clip kits connect them
automatically.

### Step-by-step: dumping the firmware with flashrom

[flashrom](https://www.flashrom.org/) is a free, open-source utility
that supports the CH341A programmer on Linux, macOS, and Windows.

**1. Install flashrom**

```bash
# Debian / Ubuntu / Raspberry Pi OS
sudo apt install flashrom

# Fedora / RHEL
sudo dnf install flashrom

# macOS (Homebrew)
brew install flashrom
```

On Windows, download a pre-built binary from
<https://www.flashrom.org/Downloads>.

**2. Power off the device**

Disconnect the print server from mains power and from the network.  Connect
the SOIC-8 clip to the flash chip (match the clip's pin-1 marker to the
dot on the chip), then plug the CH341A programmer into a USB port on your
computer.

**3. Detect the chip**

```bash
sudo flashrom -p ch341a_spi
```

flashrom will probe the chip and print its detected identity, e.g.:

```
Found Winbond flash chip "W25X40" (512 kB, SPI) on ch341a_spi.
```

If the chip is not detected, check the clip orientation and wiring before
retrying.

**4. Read (dump) the firmware**

```bash
sudo flashrom -p ch341a_spi -r firmware_chip_dump.bin
```

For higher confidence, read the chip **three times** and compare the
results — a stable flash will produce identical files each time:

```bash
sudo flashrom -p ch341a_spi -r dump1.bin
sudo flashrom -p ch341a_spi -r dump2.bin
sudo flashrom -p ch341a_spi -r dump3.bin
sha256sum dump1.bin dump2.bin dump3.bin
```

All three SHA-256 hashes should match.  Keep the verified dump as your
backup.

**5. Compare against the known-good reference image**

```bash
sha256sum firmware_chip_dump.bin PS-1206U_v8.8.bin
```

The two hashes may differ if your device is running a different firmware
revision, but either file can be used to restore the device.

### Restoring the firmware via the chip programmer

If you need to write a firmware image back to the chip (e.g. to recover a
bricked device):

```bash
# Erase the chip first, then write
sudo flashrom -p ch341a_spi -E
sudo flashrom -p ch341a_spi -w PS-1206U_v8.8.bin
```

flashrom performs an automatic read-back verification after writing by
default.

> **Important:** Ensure the image you write is exactly **512 KB (524 288 bytes)**
> and is a valid image for this hardware.  Writing an incompatible image may
> render the device unbootable.  The bundled `PS-1206U_v8.8.bin` file is the
> correct size:
>
> ```
> $ wc -c PS-1206U_v8.8.bin
> 524288 PS-1206U_v8.8.bin
> ```

---

## License

The firmware binary (`PS-1206U_v8.8.bin`) is © Edimax Technology Co., Ltd.
and is redistributed here for archival purposes.  The `backup_firmware.py`
script is released under the [MIT License](https://opensource.org/licenses/MIT).
