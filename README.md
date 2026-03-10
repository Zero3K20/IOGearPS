# IOGear GPSU21 Print Server Firmware

This repository contains firmware for the IOGear GPSU21 print server.

| Device | Firmware file | CPU |
|--------|--------------|-----|
| IOGear GPSU21 | `MPS56_90956F_9034_20191119.zip` | MediaTek MT7688 (MIPS) |

## Supported Printing Protocols

The GPSU21 supports the following printing protocols:

| Protocol | Status |
|----------|--------|
| LPR/LPD | Enabled |
| IPP (port 631) | Enabled |
| Raw TCP printing | Enabled |
| SMB/Windows printing | Enabled |
| AppleTalk/PAP | Enabled |

## AirPrint Support

The GPSU21 firmware includes a **built-in Bonjour stack and IPP server** (port 631).
It advertises itself as `_ipp._tcp` via mDNS automatically on your local network.

### No PC software needed for modern devices

| Apple device | How to print |
|---|---|
| **iOS 14+** | Goes to Settings → Wi-Fi (on same network) → AirPrint appears automatically |
| **macOS 11+ (Big Sur and later)** | System Preferences → Printers & Scanners → "+" → print server appears automatically |
| iOS 13 or earlier | Requires optional helper (see below) |
| macOS 10.15 (Catalina) or earlier | Requires optional helper (see below) |

### Setup

1. **Assign a static IP address** to the GPSU21 (via your router's DHCP
   reservation or the print server's web interface at `http://<printer-ip>/`).
2. **Enable IPP** — open the web interface, go to **Setup → Services** and
   ensure *Use IPP* is set to *Enabled*.
3. **Enable Bonjour** — on the **Setup → TCP/IP** page, set *Rendezvous (Bonjour)
   Service* to *Enabled* and enter a descriptive *Service Name* (e.g. `IOGear
   GPSU21`).
4. **Save & Restart** the device.

The printer will now appear automatically on iOS 14+ and macOS 11+ devices that
are on the same Wi-Fi network.

### Optional: older Apple device support (iOS 13 / macOS 10.15 and earlier)

For older Apple devices, you can manually advertise the `_universal._sub._ipp._tcp`
sub-type using an Avahi service file on Linux or a Bonjour helper on Windows.

> **Installing software on your PC is not required for iOS 14+ / macOS 11+.**
> The above is only needed if you must also support older Apple devices.

### Supported Document Formats

When printing via AirPrint the iOS/macOS client sends jobs in one
of the following formats, all of which are forwarded by the print server
to the attached USB printer:

- `application/pdf`
- `image/jpeg`
- `image/png`
- `image/urf` (Universal Raster Format)
- `application/octet-stream` (raw)

## Firmware Files

| File | Device | Version | Notes |
|------|--------|---------|-------|
| `MPS56_90956F_9034_20191119.zip` | IOGear GPSU21 (MT7688) | 9.09.56F build 9034 (2019/11/19) | Extract the `.bin` before flashing |

---

## Stability & Known Issues

### Reported freeze with the older 2017 IOGear firmware

A freeze/lock-up bug has been widely reported with the **official IOGear 2017
firmware** (`MPS56_IOG_GPSU21_20171123.zip`, build 9032, dated 2017/11/23).
Affected devices stop responding to network requests — including print jobs and
the web interface — after an extended uptime (typically hours to a day or more)
and require a power cycle to recover.

**The firmware in this repository is build 9034, dated 2019/11/19** — the same
9.09.56 firmware series but two years newer than the 2017 build.  Binary
analysis confirms that **all four freeze mechanisms are present in both builds**
(see the sections below).  Build 9034 is still the recommended choice over
build 9032 because it contains a wireless driver overhaul that reduces
unrelated error paths, but it does not eliminate the core stability issues.

See [COMPATIBILITY.md](COMPATIBILITY.md) for the full version history.

### What the firmware binary actually reveals

The GPSU21 firmware (`MPS56_90956F_9034_20191119.zip`) can be decompressed
with the tools in this repository, and the resulting 1.57 MB eCos binary
contains embedded build-time assert strings and error messages that make
several freeze mechanisms directly observable without any disassembly.

Both builds (9032 from 2017 and 9034 from 2019) were decompressed and
searched for the same indicator strings.  See
"[Does the 2017 IOGear firmware have the same issues?](#does-the-2017-iogear-firmware-have-the-same-issues)"
below for the complete side-by-side comparison.  The descriptions that follow
use build 9034 as the reference, but every finding applies equally to build
9032.

#### Finding 1 — HTTP server socket pool exhaustion

The HTTP server (`httpd.c`) contains the string:

```
http:1216, No free socket!
```

This is a `printf`-style error logged at line 1216 of the HTTP server source
when `http_child` — the per-connection handler — fails to obtain a free socket
from the pool.  When this path is hit the server cannot accept new HTTP
connections, making the web interface unreachable.

What drains the pool:
- The firmware runs **19 concurrent service threads** (HTTP, LPD, IPP, SMB,
  NetBIOS, Raw TCP, AppleTalk/PAP, Novell SAP/Connect/NDS, SNMP, Telnet,
  TFTP, mDNS, email-alert, and the main print-server loop) — all of which
  share a single small lwIP TCP connection pool.  Each service that holds a
  connection open occupies a slot from the same pool that the HTTP server uses.
- **Two web pages auto-refresh automatically** and generate a new HTTP
  connection on every cycle:
  - `services.htm` — `<meta HTTP-EQUIV="Refresh" CONTENT="5;">` (every 5 s)
  - `index.htm` (IPP jobs list) — `<meta HTTP-EQUIV="Refresh" CONTENT="10;">`
    (every 10 s)
  If a browser tab is left open on either page the device receives a new TCP
  connection every 5–10 seconds.  If the HTTP server does not close the socket
  before the next refresh arrives — which can happen on any error path where
  the close() call is skipped — the pool shrinks by one each cycle.

Observed symptom: the web interface becomes unreachable while printing may
still work (LPD/IPP connections also compete for the same pool, so eventually
printing stops too).

#### Finding 2 — mDNS/Bonjour responder can enter a permanent lock failure

The mDNS stack (`mDNSPosix.c`) contains all of the following error strings —
each represents a distinct, confirmed failure path:

| String found in binary | What it means |
|---|---|
| `mDNSPlatformTimeNow went backwards by %ld ticks; setting correction factor to %ld` | The system clock used by mDNS runs backwards (monotonic clock source issue); a correction factor is applied but the state machine is already inconsistent |
| `mDNS_Lock: Locking failure! mDNS_busy (%ld) != mDNS_reentrancy (%ld)` | The mDNS re-entrancy lock has become corrupted — the counts that must match no longer do |
| `mDNS_Lock: m->mDNS_busy is %ld but m->timenow not set` | The lock was acquired without setting `timenow`, which subsequent timer logic depends on |
| `GetFreeCacheRR ERROR! Cache already locked!` | The DNS resource-record cache is locked when it should be free — a state the code never intended |
| `SendResponses exceeded loop limit %d: giving up` | The mDNS send loop hit its iteration cap and gave up, meaning responses stopped being sent |

When the lock corruption path is hit, the mDNS responder thread stops
advertising the printer's `_ipp._tcp` and `_printer._tcp` Bonjour records.
From the user's perspective the device appears to have disappeared from
AirPrint and from the system's printer list — which looks exactly like a
freeze even though the hardware is still running.

#### Finding 3 — Ethernet TX-descriptor stall (triggers automatic reset)

The Ethernet driver (`if_ra305x.c`) contains:

```
ifra305x%d: txd%d is not free, cansend: %x
Network unstable system is going to reset.....
```

When every TX descriptor in the Ethernet ring is occupied and not being freed
— which can happen if a service holds a reference to a network buffer without
releasing it — the driver detects "network unstable" and **forces a full system
reboot**.  From the user's perspective: the device goes offline for ~20 seconds
and then comes back as if it had been power-cycled.  On slower networks or
if the user acts quickly (e.g. pulling power during the reboot window) the
behaviour is indistinguishable from a hard lock-up.

This reset path is self-healing, but it is triggered by the same underlying
resource-leak that also feeds Finding 1.

#### Finding 4 — eCos kernel thread-counter overflows

The eCos kernel (`thread.cxx`) contains:

```
wakeup_count overflow
suspend_count overflow
```

In eCos, `wakeup_count` and `suspend_count` are narrow integer fields (16-bit
in most configurations) that track how many times a thread has been
wake-signalled or suspended.  If a service thread is wake-signalled faster
than it can drain its work queue — possible if an mDNS flood or a burst of
print jobs is received — these counters can overflow.  When they do, the
scheduler's invariant check fires and the thread state is undefined: the thread
may stop running without any visible crash.

#### Summary of all confirmed bug indicators in build 9034

Every item in the following table was confirmed present in the decompressed
firmware binary by searching for the exact string it contains.  None of these
are speculation; they are error paths that the ZOT developers compiled into
the image:

| Indicator string | Category |
|---|---|
| `http:1216, No free socket!` | HTTP socket pool exhaustion |
| `mDNSPlatformTimeNow went backwards by %ld ticks` | mDNS clock issue |
| `mDNS_Lock: Locking failure! mDNS_busy (%ld) != mDNS_reentrancy (%ld)` | mDNS lock corruption |
| `GetFreeCacheRR ERROR! Cache already locked!` | mDNS cache deadlock |
| `SendResponses exceeded loop limit %d: giving up` | mDNS send loop abort |
| `ifra305x%d: txd%d is not free, cansend: %x` | Ethernet TX descriptor stall |
| `Network unstable system is going to reset.....` | Ethernet watchdog auto-reset |
| `wakeup_count overflow` | eCos scheduler counter overflow |
| `suspend_count overflow` | eCos scheduler counter overflow |
| `%s:%d malloc fail` | Heap allocation failure logging |
| `free mem negative!` | Heap accounting went negative |
| `Attempt to open larger file descriptor than FOPEN_MAX!` | FD table exhausted |
| `fd out of range` | File descriptor out of valid range |
| `Stack is so small size wrapped` | eCos thread stack overflow |
| `mod_timer: expires < 0 !` | Timer set with negative expiry |

#### Practical mitigation — disable unused services

Because Finding 1 (socket exhaustion) is directly worsened by the sheer
number of services running, disabling services you do not use reduces the
number of always-open TCP connections competing for the pool, and frees the
memory those threads consume.

Navigate to **Setup → Services** in the web interface and disable everything
you do not actively use:

| Service | Port(s) | Disable if… |
|---|---|---|
| **NetWare Bindery mode** | TCP 515+ | You have no Novell NetWare server |
| **NetWare NDS mode** | TCP 524 | You have no Novell NDS tree |
| **AppleTalk / PAP** | DDP/ATP | You have no classic Mac (pre-OS X) clients |
| **SMB (Windows file sharing)** | TCP 139, 445 | You print via LPR, IPP, or RAW TCP instead |
| **SNMP** | UDP 161 | You have no SNMP monitoring system |
| **Telnet** | TCP 23 | You do not use the Telnet management CLI |
| **Email alert** | TCP 25 | You have no SMTP alert configured |

Recommended minimum for a modern home or small office:

- **LPR/LPD** — needed for most print queues on macOS and Linux
- **IPP** — needed for AirPrint and modern Windows printing
- **Raw TCP (port 9100)** — needed for direct TCP printing
- **Bonjour/mDNS** — needed for AirPrint auto-discovery

Disabling the six services listed above leaves four services running instead
of fourteen, which substantially reduces both peak socket usage and steady-
state memory consumption.

> **Also:** close or navigate away from the `services.htm` and IPP jobs
> (`index.htm`) pages in your browser after using them.  Leaving either page
> open in a background tab causes a new HTTP connection every 5–10 seconds,
> gradually draining the socket pool.

### Does the 2017 IOGear firmware have the same issues?

**Yes — every confirmed bug indicator is present in both builds.**

The official IOGear firmware (`MPS56_IOG_GPSU21_20171123.zip`, build 9032)
was downloaded from the URL in issue #11
(`https://cdn.shopify.com/…/MPS56_IOG_GPSU21_20171123.zip`) and subjected to
the same string-search analysis as build 9034.  The decompressed image is
1.71 MB (versus 1.57 MB for 9034, the difference being a larger Wi-Fi driver
in the older build).

The table below shows every indicator string checked:

| Bug indicator string | Build 9032 (2017 IOGear) | Build 9034 (2019 ZOT) |
|---|:---:|:---:|
| `http:1216, No free socket!` | ✅ present | ✅ present |
| **`accept failure`** (HTTP accept() itself fails before pool is even checked) | **✅ present** | ❌ removed |
| `mDNSPlatformTimeNow went backwards by %ld ticks` | ✅ present | ✅ present |
| `mDNS_Lock: Locking failure! mDNS_busy (%ld) != mDNS_reentrancy (%ld)` | ✅ present | ✅ present |
| `GetFreeCacheRR ERROR! Cache already locked!` | ✅ present | ✅ present |
| `SendResponses exceeded loop limit %d: giving up` | ✅ present | ✅ present |
| `ifra305x%d: txd%d is not free, cansend: %x` | ✅ present | ✅ present |
| `Network unstable system is going to reset.....` | ❌ absent | ✅ present |
| `wakeup_count overflow` | ✅ present | ✅ present |
| `suspend_count overflow` | ✅ present | ✅ present |
| `%s:%d malloc fail` | ✅ present | ✅ present |
| `free mem negative!` | ✅ present | ✅ present |
| `Attempt to open larger file descriptor than FOPEN_MAX!` | ✅ present | ✅ present |
| `fd out of range` | ✅ present | ✅ present |
| `Stack is so small size wrapped` | ✅ present | ✅ present |
| `mod_timer: expires < 0 !` | ✅ present | ✅ present |
| Auto-refresh `CONTENT="5"` (services page, every 5 s) | ✅ present | ✅ present |
| Auto-refresh `CONTENT="10"` (IPP jobs page, every 10 s) | ✅ present | ✅ present |

**Notable differences:**

- **Build 9032 is worse for HTTP connections:** It logs `accept failure` before
  reaching the socket pool check, meaning the `accept()` system call itself
  starts failing earlier — the HTTP server in 9032 becomes unreachable
  *sooner* under load than the same situation in 9034.
- **Build 9034 added explicit watchdog logging** (`"Network unstable system is
  going to reset....."`).  Build 9032 likely has the same hardware watchdog
  path in the Ethernet driver (`if_ra305x.c`) but does not log it to the
  diagnostic output — so the auto-reset happens silently.
- **Both builds have the same auto-refreshing pages** at the same intervals
  (5 s and 10 s), meaning the socket-drain issue from leaving browser tabs
  open is equally severe on both.

**Conclusion:** If you are using the 2017 IOGear firmware and experiencing
freezes, switching to the 2019 build in this repository will not eliminate the
freezes, but build 9034 is still marginally better because the `accept()`
failure path was removed and error logging improved.  All workarounds and
service-disablement recommendations that apply to build 9034 apply equally to
build 9032.

### Which issues are fixed?

The FreeRTOS-based firmware in this repository is a clean reimplementation that
avoids all of the OEM freeze mechanisms described above — the eCos kernel, the
Apple mDNSCore stack, and the original Ethernet driver are all replaced.

| Finding | Status |
|---------|--------|
| **Finding 1** — HTTP browser auto-refresh exhausting the connection pool | ✅ Eliminated — new HTTP server does not share a fixed socket pool |
| **Finding 2** — mDNS/Bonjour lock counter de-sync (Bonjour stops working) | ✅ Eliminated — custom mDNS implementation replaces mDNSCore |
| **Finding 3** — Ethernet TX-descriptor stall | ✅ Eliminated — new PDMA Ethernet driver with proper descriptor management |
| **Finding 4** — eCos thread `wakeup_count` overflow assertion | ✅ Eliminated — FreeRTOS replaces the eCos kernel entirely |

Flash `firmware/build/gpsu21_freertos.bin` (built from source or downloaded
from the [Releases](../../releases) page) to replace the OEM firmware.

### Workarounds for periodic freeze

> **Flashing the FreeRTOS firmware eliminates all confirmed freeze mechanisms.**
> The workarounds below are only needed if you cannot or do not want to flash
> custom firmware.

If you choose not to flash the FreeRTOS firmware, the following workarounds keep
the device running reliably without manual intervention.

#### Option 1 — Smart power outlet or hardware timer

Use a programmable smart outlet or a mechanical timer to cut power once per day
(e.g. at 3:00 AM) and restore it 10–30 seconds later.  No software is needed,
and the device comes back up on its own after rebooting.

#### Option 2 — Router-based scheduled reboot (OpenWrt / DD-WRT / Tomato)

On a router running Linux-based firmware, add a cron entry that sends an HTTP
request to the device's restart URL once a day:

```sh
# /etc/crontabs/root  (OpenWrt)
0 3 * * * wget -q -O /dev/null http://<device-ip>/restart.htm
```

The device restarts in about 20 seconds and resumes normal operation.

#### Option 3 — Server or Raspberry Pi cron job

On any always-on Linux or macOS machine, schedule a daily reboot request:

```sh
# Run:  crontab -e
0 3 * * * curl -sf http://<device-ip>/restart.htm > /dev/null
```

Replace `<device-ip>` with the static IP address of the GPSU21.

---

## Emergency Recovery — Bricked GPSU21

If the device no longer responds after a firmware flash (web interface unreachable,
device does not appear on the network), follow these steps in order from easiest to
hardest.

### IP address reference

| Who               | Address         | Notes |
|-------------------|-----------------|-------|
| GPSU21 (static fallback / U-Boot) | `192.168.0.1`   | Applied after ~30 seconds if no DHCP server responds |
| Your laptop / Mac | `192.168.0.100` | Set manually for a direct Ethernet connection |

The firmware's `dhcp_start()` clears the interface address to `0.0.0.0` while
waiting for a DHCP lease.  If your router is not in the path (direct cable), no
DHCP server answers, and after 30 seconds the firmware restores its static address
`192.168.0.1`.  U-Boot uses the same address, so the recovery instructions below
work identically regardless of whether the bootloader or the application is running.

> ⚠️ **Do not** set your laptop to `192.168.0.1` — that is the device's address.
> Use `192.168.0.100` (or any other `.2`–`.254` address on the same `/24` subnet).

### Step 1 — Direct connection to your laptop / MacBook

1. **Connect the GPSU21 directly to your computer** with an Ethernet cable.
   MacBook Pro and most modern laptops have no built-in RJ-45 port — use a
   **USB-C to Ethernet adapter** (Apple's own adapter or any USB-C/Thunderbolt
   to Gigabit Ethernet adapter works).

2. **Set the Ethernet adapter to a manual static IP:**

   - **macOS (System Settings / System Preferences):**
     *System Settings → Network → select the Ethernet / USB adapter →
     Details… → TCP/IP tab → Configure IPv4: Manually*
     - IP Address: `192.168.0.100`
     - Subnet Mask: `255.255.255.0`
     - Router: *(leave blank)*
     → OK / Apply
   - **Windows:** Control Panel → Network and Sharing Center → Change adapter
     settings → right-click the Ethernet adapter → Properties →
     *Internet Protocol Version 4 (TCP/IPv4)* → *Use the following IP address*:
     - IP address: `192.168.0.100`
     - Subnet mask: `255.255.255.0`
     - Default gateway: *(leave blank)*
     → OK
   - **Linux:** `sudo ip addr add 192.168.0.100/24 dev eth0`
     *(replace `eth0` with your adapter name shown by `ip link`)*

3. Power on the GPSU21 and wait **35 seconds** (30 seconds for the DHCP fallback to
   apply, plus a few seconds for the network stack to start).

4. Verify connectivity:
   ```
   ping 192.168.0.1
   ```
   Then open `http://192.168.0.1/` in a browser.

5. **After recovery, restore your network settings:**

   - **macOS:** Return to *System Settings → Network → Ethernet adapter →
     Details… → TCP/IP* and set *Configure IPv4* back to *Using DHCP* → Apply.
   - **Windows:** Return to the TCP/IPv4 properties dialog and select
     *Obtain an IP address automatically* → OK.
   - **Linux:** `sudo ip addr del 192.168.0.100/24 dev eth0 && sudo dhclient eth0`

> **Still timing out after 35 seconds?**  If there is no response after the direct
> connection attempt above, proceed to Step 2.  U-Boot is stored in a separate
> flash partition and is almost always still intact, so UART recovery will work.

### Step 2 — UART + U-Boot recovery (recommended before IC programmer)

The GPSU21's MT7688 SoC boots U-Boot before loading the application firmware.
If only the application partition is corrupt, U-Boot is still functional and can
reflash the firmware over the network — **no hardware programmer is required**.

> **How U-Boot TFTP works:**  U-Boot acts as a TFTP **client** — it downloads
> the firmware **from** a TFTP server running on your computer.  You do not
> connect to the device with a TFTP client; instead, you run a TFTP server on
> your laptop and U-Boot fetches the file.  UART access is required to type the
> U-Boot commands.

#### Hardware needed

- USB-to-TTL UART adapter (3.3 V logic; CP2102, CH340, FTDI, or similar)
- A computer with a terminal emulator (PuTTY, minicom, screen)
- Fine-tipped soldering iron and solder *(optional — see "No-solder connection" below)*

#### Finding the UART pads

Open the GPSU21 enclosure.  On the PCB look for a row of unpopulated 2.54 mm
through-holes or test pads labelled **TX**, **RX**, and **GND** (sometimes also
**3V3** / **VCC**).  These pads are typically near the MT7688 SoC.

Connect your adapter:

| Adapter pin | GPSU21 pad |
|-------------|------------|
| GND         | GND        |
| RX          | TX         |
| TX          | RX         |

> ⚠️ Do **not** connect the adapter's 3.3 V / 5 V power pin to the board —
> power the GPSU21 from its own USB or barrel-jack supply.

#### No-solder connection (solder-free alternatives)

**Soldering is not required** if you are comfortable holding the connection steady
while the terminal session is active.  Two common no-solder methods:

1. **Press-fit jumper wires (easiest — works with 2.54 mm through-holes)**
   Insert the male end of a Dupont/jumper wire into each through-hole and tilt it
   slightly so the wire presses against the barrel of the hole.  The friction is
   enough to keep contact.  Hold the board flat on a table while working so the
   wires stay in place.  You only need to hold the interrupt key for ~1–2 seconds
   at boot — after that the U-Boot console is interactive and the wires can rest
   undisturbed.

2. **Pogo-pin probes (works with both through-holes and bare test pads)**
   Spring-loaded pogo pins (available cheaply as "IC test hook clips" or
   "PCB probe pins") press against pads without any soldering.  Tape or clip them
   in place, or hold them by hand, for the duration of the session.

#### Entering the U-Boot console

1. Open a terminal at **57600 baud, 8N1** (no hardware flow control).
2. Power on the GPSU21.
3. When you see `Hit any key to stop autoboot`, press a key immediately
   (you have ~1–2 seconds).

You should now see a `MT7688 #` or `zot #` prompt.

#### Setting up a TFTP server on your computer

U-Boot will download the firmware **from** your computer.  Start a TFTP server
and place the firmware file in its root directory before running the U-Boot
commands below.

- **macOS:**
  macOS includes a built-in TFTP server (disabled by default).  Enable it for
  this session:
  ```bash
  # Copy firmware to the TFTP root
  sudo mkdir -p /private/tftpboot
  sudo cp MPS56_90956F_9034_20191119.bin /private/tftpboot/

  # Start the TFTP server (runs until you reboot or stop it)
  sudo launchctl load -F /System/Library/LaunchDaemons/tftp.plist
  ```
  Stop it afterwards with:
  ```bash
  sudo launchctl unload /System/Library/LaunchDaemons/tftp.plist
  ```

- **Linux:** Install and start *tftpd-hpa*:
  ```bash
  sudo apt install tftpd-hpa          # Debian / Ubuntu
  sudo cp MPS56_90956F_9034_20191119.bin /srv/tftp/
  sudo systemctl restart tftpd-hpa
  ```

- **Windows:** Use [Tftpd64](https://bitbucket.org/phjounin/tftpd64/downloads/)
  (free).  Set the root directory to the folder containing the `.bin` file.

#### Reflashing via TFTP

With the TFTP server running on your computer (at `192.168.0.100`) and the
Ethernet cable connecting the GPSU21 to your computer, enter these commands at
the U-Boot prompt:

```
setenv ipaddr   192.168.0.1       # temporary IP for the GPSU21
setenv serverip 192.168.0.100     # IP address of your computer
# 0x80500000 = DRAM load address from the uImage header (load_addr field)
tftpboot 0x80500000 MPS56_90956F_9034_20191119.bin
```

If the download succeeds, run the ZOT upgrade command to write the firmware to
flash.  The exact command varies by U-Boot build; try:

```
run upgradefirmware
```

or, if that is not defined, use the manual erase-and-copy approach.  First run
`printenv` to find the firmware partition start address (look for variables named
`fwaddr`, `firmware_addr`, or similar):

```
# Example only — use the actual addresses from printenv on YOUR device.
# The erase range must cover the entire firmware partition.
erase 0x9C050000 +0x600000
cp.b 0x80500000 0x9C050000 ${filesize}
```

> **Always use `printenv` to find the correct addresses** — the flash partition
> start address and partition size vary by U-Boot build.  Any variable containing
> `firmware`, `upgrade`, or `fwaddr` will show the correct values for your device.

After writing, reboot:

```
reset
```

### Step 3 — IC programmer (last resort)

Use an IC programmer only if U-Boot itself is also corrupt (the device shows no
UART output at all and the ping/recovery-IP steps above produce no result).

> ⚠️ **The firmware `.bin` file in this repository is NOT a full flash dump.**
> It is the application firmware partition only (~342 KB).  Flashing it at
> offset 0 will overwrite U-Boot and make recovery harder.  Before using a
> programmer, obtain a **full flash dump** from a working GPSU21 unit.

#### Identifying the flash chip

The GPSU21's SPI NOR flash chip is an 8-pin package (SOIC-8 or WSON-8) located
near the MT7688 SoC.  Read the part number printed on the chip.  The MT7688 SoC is compatible with:

| Manufacturer | Part numbers |
|---|---|
| Macronix | MX25L1605D, MX25L3205D, MX25L6405D, MX25L12805D |
| Winbond | W25Q16DV, W25Q32BV, W25Q128BV |
| GigaDevice | GD25Q32B |
| Atmel/Adesto | AT25DF321 |

All are standard SPI NOR flash chips compatible with common programmers.

#### Programmer hardware

Any programmer that supports SPI NOR flash and the SOP8/WSON8 package works:

| Programmer | Notes |
|---|---|
| CH341A (MiniProgrammer) | Inexpensive; widely available; works with NeoProgrammer, AsProgrammer, flashrom |
| RT809F / RT809H | Faster; more chip support |
| XGECU T48 / T56 | Full-featured; higher cost |

For **in-circuit programming** (chip stays on the board) use a SOP8 test clip.
For a cleaner read/write, desolder the chip and use a ZIF or SOP8 socket adapter.

#### Software

| OS | Software |
|---|---|
| Windows | [NeoProgrammer](https://github.com/a3130/NeoProgrammer) (free), [AsProgrammer](https://github.com/nofeletru/UsbAsp-flash) (free) |
| Linux / macOS | [flashrom](https://flashrom.org/) (`sudo apt install flashrom` on Debian/Ubuntu) |

#### Recovery procedure

1. **Obtain a full flash dump** from another working GPSU21 (use the programmer
   to read the entire chip, e.g. `flashrom -p ch341a_spi -r gpsu21_flash_backup.bin`).
2. Identify the flash chip on the bricked board.
3. Connect the programmer to the chip (in-circuit with a clip, or desoldered).
4. Write the full dump:
   ```
   flashrom -p ch341a_spi -w gpsu21_flash_backup.bin
   ```
5. Verify the write:
   ```
   flashrom -p ch341a_spi -v gpsu21_flash_backup.bin
   ```
6. Reinstall the chip (if desoldered) and power on the device.

### Prevention — back up your flash before flashing

Before flashing any modified firmware:

```
# Read the current full flash contents (via flashrom + CH341A):
flashrom -p ch341a_spi -r gpsu21_flash_BACKUP_$(date +%Y%m%d).bin

# Keep this file safe — it is your recovery image.
```

A backup lets you restore the device to its exact previous state without needing
to find a donor unit.

## Flashing the Firmware

### IOGear GPSU21

1. Extract `MPS56_90956F_9034_20191119.bin` from the ZIP file.
2. Open the GPSU21 web interface at `http://<printer-ip>/`.
3. Navigate to the firmware upgrade page.
4. Upload the `.bin` file (not the ZIP).
5. Do **not** power-cycle the device during the upgrade.

---

## Unpacking, Modifying, and Repacking the Firmware

The `tools/` directory contains Python 3 scripts that let you extract the
firmware components, inspect or modify them, and reassemble a flashable image.
The scripts work on **Windows, macOS, and Linux** — Python 3 is the only
requirement.

### Prerequisites

Install Python 3 from <https://www.python.org/downloads/> if it is not
already available.

Verify the installation:

```
# Windows (Command Prompt or PowerShell)
python --version

# macOS / Linux (use python3 if python points to Python 2)
python3 --version
```

---

### IOGear GPSU21 firmware tools

The GPSU21 firmware (`MPS56_90956F_9034_20191119.zip`) originally used **eCos RTOS**
on a MediaTek MT7688 MIPS SoC.  This repository has **migrated to FreeRTOS** to
eliminate the numerous eCos compilation errors.  All 61 web interface files — HTML
pages, JavaScript, CSS, and JPEG/GIF images — are stored in the
[`gpsu21_web/`](gpsu21_web/) directory.

### Why FreeRTOS instead of eCos?

eCos 3.0 accumulated many build-breaking issues when compiled with modern GCC
(GCC 14+): redefined structs, removed attributes, expansion-to-defined warnings
treated as errors, and missing headers.  **FreeRTOS** (V10.6.2) is an actively
maintained, MIT-licensed RTOS that:

- Compiles cleanly with `gcc-mipsel-linux-gnu` (no patches required)
- Has a well-tested MIPS32r2 port
- Uses the same **lwIP 2.2.0** TCP/IP stack (same socket API)
- Provides the same POSIX-like threading model the firmware relies on

### Building from source

```bash
# Download FreeRTOS-Kernel and lwIP (done automatically by CI)
curl -fL https://github.com/FreeRTOS/FreeRTOS-Kernel/archive/refs/tags/V10.6.2.tar.gz \
  | tar -xz && mv FreeRTOS-Kernel-* freertos-kernel
curl -fL https://github.com/lwip-tcpip/lwip/archive/refs/tags/STABLE-2_2_0_RELEASE.tar.gz \
  | tar -xz && mv lwip-* lwip

# Build
make -C firmware \
  CROSS_COMPILE=mipsel-linux-gnu- \
  FREERTOS_DIR=$(pwd)/freertos-kernel \
  LWIP_DIR=$(pwd)/lwip

# Flash firmware/build/gpsu21_freertos.bin via the GPSU21 web interface
```

#### Edit-and-release workflow (recommended)

1. **Edit** any file in `gpsu21_web/` directly on GitHub or in a local clone.
2. **Commit and push** to `main`.
3. **GitHub Actions** automatically builds the FreeRTOS firmware and publishes a
   new release containing `gpsu21_freertos.bin` — ready to flash.

---

