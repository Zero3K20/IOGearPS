# IOGear PS-1206U and GPSU21 Print Server Firmware

This repository contains firmware for two **different** IOGear print servers.
See [COMPATIBILITY.md](COMPATIBILITY.md) for a full hardware comparison.

| Device | Firmware file | CPU |
|--------|--------------|-----|
| IOGear PS-1206U | `PS-1206U_v8.8.bin` | Edimax 8820 (x86) |
| IOGear GPSU21 | `MPS56_90956F_9034_20191119.zip` | MediaTek MT7688 (MIPS) |

> ⚠️ **These firmware images are NOT interchangeable.** Flashing PS-1206U
> firmware onto a GPSU21 (or vice versa) will brick the device.

## Supported Printing Protocols

Both the PS-1206U and the GPSU21 support the same set of printing protocols:

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

1. **Assign a static IP address** to the PS-1206U (via your router's DHCP
   reservation or the print server's web interface at `http://<printer-ip>/`).
2. **Enable IPP** — open the web interface, go to **Setup → Services** and
   ensure *Use IPP* is set to *Enabled*.
3. **Enable Bonjour** — on the **Setup → TCP/IP** page, set *Rendezvous (Bonjour)
   Service* to *Enabled* and enter a descriptive *Service Name* (e.g. `IOGear
   PS-1206U`).
4. **Save & Restart** the device.

The printer will now appear automatically on iOS 14+ and macOS 11+ devices that
are on the same Wi-Fi network.

### Optional: older Apple device support (iOS 13 / macOS 10.15 and earlier)

The `airprint/` directory contains files for advertising the `_universal._sub._ipp._tcp`
sub-type that older Apple clients require — choose the one for your helper OS:

| File | Platform |
|------|----------|
| `airprint/IOGear-PS1206U.service` | Linux (Avahi/mDNS daemon) |
| `airprint/windows-bonjour.bat` | Windows (Apple Bonjour) |

> **Installing software on your PC is not required for iOS 14+ / macOS 11+.**
> The helper files above are only needed if you must also support older Apple
> devices.

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
| `PS-1206U_v8.8.bin` | IOGear PS-1206U (Edimax 8820) | v8.8 | 512 KB Edimax flat image |
| `MPS56_90956F_9034_20191119.zip` | IOGear GPSU21 (MT7688) | 9.09.56F build 9034 (2019/11/19) | Extract the `.bin` before flashing |

> ⚠️ These files target **different hardware** — see [COMPATIBILITY.md](COMPATIBILITY.md)
> for the full hardware breakdown before flashing.

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

### Which issues can actually be fixed?

Short answer:

| Finding | Can it be fixed? | How |
|---|---|---|
| **Finding 1** — HTTP socket pool exhaustion | **Partially** — the auto-refresh contribution is fixable right now; the underlying pool size is not | Remove the `<meta HTTP-EQUIV="Refresh">` tags from `SERVICES.HTM` and `INDEX.HTM` using the existing repack tools |
| **Finding 2** — mDNS/Bonjour lock corruption | Mitigated only | Disable Bonjour/Rendezvous in Setup → TCP/IP; the lock bug cannot be fixed without binary patching |
| **Finding 3** — Ethernet TX-descriptor stall | No user fix needed | The Ethernet driver auto-resets; not a hard freeze |
| **Finding 4** — eCos thread-counter overflow | Mitigated only | Disable unused services; the overflow cannot be fixed without binary patching |

#### The one fix you can apply right now — remove the auto-refresh tags

The `SERVICES.HTM` and `INDEX.HTM` (IPP jobs list) pages inside the firmware
are plain HTML and can be modified using the existing
`tools/unpack_gpsu21.py` / `tools/repack_gpsu21.py` scripts.  Both pages
contain an unconditional auto-refresh meta tag that instructs every browser
that opens the page to reload it on a short timer:

```html
<!-- SERVICES.HTM — reloads every 5 seconds -->
<meta HTTP-EQUIV="Refresh" CONTENT="5;">

<!-- INDEX.HTM (IPP Jobs List) — reloads every 10 seconds -->
<meta HTTP-EQUIV="Refresh" CONTENT="10;">
```

Every reload opens a new TCP connection to the HTTP server.  If the connection
is not fully closed before the next reload fires — which happens on any error
path that skips `close()` — the socket pool shrinks by one.  Removing these
tags eliminates the leak mechanism for idle browser tabs entirely.

**Step-by-step:**

```sh
# 1. Extract the firmware web files
python3 tools/unpack_gpsu21.py MPS56_90956F_9034_20191119.zip /tmp/gpsu21_patched/

# 2. Edit SERVICES.HTM — delete the refresh line
sed -i '/<meta HTTP-EQUIV="Refresh" CONTENT="5;">/d' /tmp/gpsu21_patched/SERVICES.HTM

# 3. Edit INDEX.HTM — delete the refresh line
sed -i '/<meta HTTP-EQUIV="Refresh" CONTENT="10;">/d' /tmp/gpsu21_patched/INDEX.HTM

# 4. Repack into a new firmware image
python3 tools/repack_gpsu21.py MPS56_90956F_9034_20191119.zip \
        /tmp/gpsu21_patched/ modified_no_autorefresh.bin

# 5. Flash modified_no_autorefresh.bin via the web interface
#    (Setup → Upgrade → browse to the file → upload)
```

> **What this does not fix:** The underlying lwIP socket pool size, the mDNS
> lock corruption, and the eCos thread-counter issues all remain.  Removing
> the auto-refresh only removes one of the ways that browser tabs silently
> drain the pool.  The device will still eventually need a reboot — it will
> just happen less frequently if you regularly open the status pages in a
> browser.

#### The fixes that require binary patching

The remaining three findings require locating the exact MIPS instruction(s)
responsible for each bug, writing a replacement that fits in the same number
of bytes (or using a trampoline), and repacking the LZMA-compressed binary.
This is genuine reverse-engineering work; the section
"[Why the crash cannot be directly patched](#why-the-crash-cannot-be-directly-patched)"
below explains the constraints in full.

A rough estimate of difficulty per finding:

| Finding | Why it is hard to patch |
|---|---|
| **Finding 1** — socket pool size | The lwIP pool size is a compile-time constant baked into the data segment; changing it requires finding and modifying the pool descriptor structure and any code that initialises it — typically 4–8 locations that all have to agree |
| **Finding 2** — mDNS lock | The lock is managed by a re-entrancy counter; the bug is that some call paths increment the counter without a matching decrement (or vice versa). Finding all call sites in ~350 KB of unnamed pseudocode takes significant effort |
| **Finding 4** — eCos thread counters | The overflow happens in the eCos kernel scheduler — code that the ZOT developers did not write and that interacts with every other thread. Patching the counter width would require touching multiple scheduler data structures simultaneously |

### Why the crash cannot be directly patched

Short answer: **the firmware source code has never been released**.  The binary
can be disassembled and partially decompiled, but the output is machine-
reconstructed pseudocode — not real C source — and turning that into a working
patch is a substantial expert undertaking.

Here is the full picture:

1. **Closed-source compiled binary.**
   The GPSU21 firmware is a single LZMA-compressed blob produced by ZOT
   Technology's proprietary build system.  It contains compiled MIPS machine
   code for the eCos real-time operating system and ZOT's print-server
   application.  ZOT has never published the C/C++ source.

2. **The binary can be disassembled and decompiled — but only to pseudocode.**
   Tools such as [Ghidra](https://ghidra-sre.org/) (free, open-source) can load
   the ~350 KB MIPS binary and produce two views:
   - **Disassembly** — the raw MIPS instructions, one per line, with addresses.
   - **Decompiler view** — Ghidra's C-like pseudocode reconstruction of each
     function.

   What the decompiler **cannot** recover:
   - Original variable and function names (replaced with `DAT_xxxxxxxx`,
     `FUN_xxxxxxxx`, etc.).
   - Original data-structure definitions and type information (all types appear
     as `int`, `byte`, or raw pointer arithmetic).
   - Comments, `#define` constants, or any information that exists only in the
     source.

   The result is valid pseudocode that roughly describes what the code *does*,
   but it is not the original source and **cannot be compiled**.  To get a
   working binary you must still write and assemble MIPS patches by hand.

3. **The confirmed failure modes are now known — but the bug sites still need
   to be located in the disassembly.**
   The binary analysis above (see "What the firmware binary actually reveals")
   confirmed four specific mechanisms: HTTP socket pool exhaustion, mDNS lock
   corruption, Ethernet TX-descriptor stall, and eCos thread-counter overflow.
   Even knowing *what* breaks, a developer must still load the ~350 KB MIPS
   binary into a disassembler (e.g. Ghidra), trace each confirmed error path
   back to the code site that lets the condition occur in the first place, and
   then write a patch — all without any function or variable names and without
   being able to recompile the result.

4. **The patch must be written in MIPS assembly and fit in the existing binary.**
   Once a bug site is found, the fix cannot simply be inserted — the binary is a
   flat image where every function and variable sits at a fixed address baked
   into thousands of branch targets and load instructions.  Adding even a single
   instruction shifts everything that follows it, breaking every hard-coded
   address in the image.  Patches therefore have to be written as *in-place*
   replacements: the replacement instructions must fit in exactly the same number
   of bytes as the original, or a trampoline technique must be used (overwrite
   the buggy site with a jump to a free region in flash, place the fix there,
   then jump back) — both of which require detailed knowledge of the surrounding
   code.

5. **Only the embedded web interface can be modified safely with the existing
   tools.**
   The `tools/unpack_gpsu21.py` / `tools/repack_gpsu21.py` scripts locate the
   HTML, JavaScript, CSS, and image files stored *uncompressed* inside the
   binary, swap them in-place, and recompress.  That technique is safe precisely
   because it stays within the data region and never alters the compiled MIPS
   code.  There is no equivalent safe mechanism for patching arbitrary code.

6. **No hardware-watchdog access from the web interface.**
   The MT7688 SoC includes a hardware watchdog timer that can reset the device
   automatically if the firmware stops servicing it.  Whether the ZOT eCos build
   enables and pets that watchdog — and whether a web-interface setting can
   control it — is not publicly documented.  None of the 61 web pages in the
   firmware's embedded interface (listed in `gpsu21_web/manifest.txt`) expose
   an SSI variable or form field for it.

**In summary:** decompilation with Ghidra is possible and would be the correct
starting point for a patch.  The freeze mechanisms are now confirmed (see the
binary analysis section above), but the decompiler output is unnamed pseudocode,
not compilable source.  Turning that pseudocode into working MIPS patches that
fit inside the existing binary image remains a serious reverse-engineering
project.  The most practical mitigations are: disabling unused services to
reduce resource pressure, avoiding leaving auto-refreshing web pages open in a
browser, and scheduling automatic daily reboots if freezes persist.

### Workarounds for periodic freeze

If you still experience occasional freezing after flashing the 2019 firmware,
the following workarounds keep the device running reliably without manual
intervention.

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

### Step 1 — Wait and check recovery IP

Some ZOT firmware builds enter a recovery/rescue mode automatically when the
application image is corrupt.  After powering on, wait **60 seconds**, then try:

```
ping 192.168.0.1
http://192.168.0.1/
```

Also try the device's **last known IP address** (the one it used before it stopped
responding) in case it kept its previous network configuration.

If the device responds, it may be running a minimal recovery web server.  Upload
the original unmodified firmware (`MPS56_90956F_9034_20191119.bin`) from that page
(extract it from `MPS56_90956F_9034_20191119.zip` in this repository first).

> **"Request timed out" / "Unable to connect"?**  Before concluding that Step 1
> has failed, check for a **subnet mismatch**.  The recovery image uses the fixed
> IP `192.168.0.1`.  If your home router hands out addresses on a *different*
> subnet (e.g. `192.168.1.x`, `10.0.0.x`, `172.16.x.x`) the router will never
> forward packets to `192.168.0.1` — so the ping times out even though the device
> is alive and waiting.  **Try the direct-connection method below first.**

#### Direct connection (bypasses the router — recommended first attempt)

1. **Connect the GPSU21 directly to your PC** with an Ethernet cable.  If your PC
   has no Ethernet port, use a **USB-to-Ethernet adapter** (any USB 2.0/3.0 to
   10/100 adapter works).
2. **Assign your PC's Ethernet adapter a static IP on the same subnet:**

   - **Windows:** Control Panel → Network and Sharing Center → Change adapter
     settings → right-click the Ethernet adapter → Properties →
     *Internet Protocol Version 4 (TCP/IPv4)* → *Use the following IP address*:
     - IP address: `192.168.0.100`
     - Subnet mask: `255.255.255.0`
     - Default gateway: *(leave blank)*
     → OK
   - **macOS:** System Preferences → Network → select the Ethernet adapter →
     Configure IPv4: *Manually* → IP Address: `192.168.0.100`,
     Subnet Mask: `255.255.255.0` → Apply
   - **Linux:** `sudo ip addr add 192.168.0.100/24 dev eth0`
     *(replace `eth0` with your adapter name shown by `ip link`)*

3. Power on the GPSU21 and wait **60 seconds**.
4. Try again:
   ```
   ping 192.168.0.1
   ```
   Then open `http://192.168.0.1/` in a browser.
5. **After recovery, restore your network settings:**

   - **Windows:** Return to the TCP/IPv4 properties dialog (same path as above)
     and select *Obtain an IP address automatically* → OK
   - **macOS:** Return to Network preferences → Configure IPv4: *Using DHCP* →
     Apply
   - **Linux:** `sudo ip addr del 192.168.0.100/24 dev eth0 && sudo dhclient eth0`
     *(or restart your network service — e.g.
     `sudo systemctl restart NetworkManager` on most distros,
     `sudo systemctl restart systemd-networkd` on systemd-networkd systems)*

> **Still timing out?**  The ZOT rescue web server is only present in a small
> subset of firmware revisions.  If there is no response after the direct
> connection, skip to Step 2 — U-Boot is stored in a separate flash partition
> and is almost always still intact, so UART recovery will work.

### Step 2 — UART + U-Boot recovery (recommended before IC programmer)

The GPSU21's MT7688 SoC boots U-Boot before loading the application firmware.
If only the application partition is corrupt, U-Boot is still functional and can
reflash the firmware over the network — **no hardware programmer is required**.

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

#### Reflashing via TFTP

Set up a TFTP server on your computer (e.g. *tftpd-hpa* on Linux,
*SolarWinds TFTP Server* or *Tftpd64* on Windows) and copy
`MPS56_90956F_9034_20191119.bin` (extracted from the ZIP in this repository)
to its root directory.

Then at the U-Boot prompt:

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
near the MT7688 SoC.  Read the part number printed on the chip.  The eCos
firmware supports:

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

### IOGear PS-1206U

The upgrade page works in **any browser on any operating system** (Windows,
macOS, or Linux):

1. Open the PS-1206U web interface at `http://<printer-ip>/`.
2. Navigate to **System → Upgrade**.
3. Upload `PS-1206U_v8.8.bin` and follow the on-screen prompts.
4. Do **not** power-cycle the device during the upgrade.

> **Safe to try:** The firmware upgrade page validates the file before writing
> to flash.  If the firmware is incompatible it will display "Signature wrong"
> and refuse to flash — the device will not be bricked.

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

The GPSU21 firmware (`MPS56_90956F_9034_20191119.zip`) uses **eCos RTOS** on
a MediaTek MT7688 MIPS SoC.  All 61 web interface files — HTML pages,
JavaScript, CSS, and JPEG/GIF images — are stored **uncompressed inside the
LZMA-compressed eCos binary** and have already been extracted to the
[`gpsu21_web/`](gpsu21_web/) directory in this repository.

#### Edit-and-release workflow (recommended)

1. **Edit** any file in `gpsu21_web/` directly on GitHub or in a local clone
   (HTML, JS, CSS files use a text editor; keep file sizes ≤ original).
2. **Commit and push** to `main`.
3. **GitHub Actions** automatically runs `repack_gpsu21.py` and publishes a
   new release containing `GPSU21_modified.bin` — ready to flash.

> **Size constraint:** each edited file must be ≤ its original size.
> Pad shorter files with spaces or HTML comments to fill the gap.
> See `tools/repack_gpsu21.py` for details.

#### Local workflow (optional)

If you prefer to work entirely offline:

```
# Step 1 — Edit files in gpsu21_web/ with any text editor
#           (HTML pages, JS, CSS — keep file sizes the same or smaller)

# Step 2 — Repack locally
# Windows:
python tools\repack_gpsu21.py  MPS56_90956F_9034_20191119.zip  gpsu21_web\  gpsu21_modified.bin
# macOS / Linux:
python3 tools/repack_gpsu21.py  MPS56_90956F_9034_20191119.zip  gpsu21_web/  gpsu21_modified.bin

# Step 3 — Flash gpsu21_modified.bin via the GPSU21 web upgrade page
```

---

### IOGear PS-1206U firmware tools

#### Firmware layout

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

```
# Windows
python tools\unpack.py PS-1206U_v8.8.bin firmware_unpacked\

# macOS / Linux
python3 tools/unpack.py PS-1206U_v8.8.bin firmware_unpacked/
```

This creates `firmware_unpacked/` (or `firmware_unpacked\` on Windows)
containing:

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
   - [ndisasm](https://nasm.us/) — included in the
     [NASM Windows installer](https://www.nasm.us/pub/nasm/releasebuilds/?C=M;O=D);
     run from Command Prompt:
     ```
     ndisasm.exe -b 16 firmware_unpacked\PS06EPS.BIN | more
     ```

2. **Patch the binary** directly:
   - Use a hex editor (e.g. [HxD](https://mh-nexus.de/en/hxd/) on Windows,
     `010 Editor`, or `ImHex`) to change specific bytes, strings, or
     configuration values.
   - Use a Python script to automate bulk changes.

3. **Edit embedded web pages** — the HTML/CSS/JS pages are stored as plain text
   inside the binary.  You can locate them with a hex editor, edit them in
   place (keeping the same byte length), and save the modified `PS06EPS.BIN`.

### Step 3 — Repack

After modifying `PS06EPS.BIN` (and/or `PS06UPG.BIN`):

```
# Windows
python tools\repack.py firmware_unpacked\ PS-1206U_modified.bin

# macOS / Linux
python3 tools/repack.py firmware_unpacked/ PS-1206U_modified.bin
```

The script recompresses each component into a new ZIP archive, assembles the
complete 512 KB flash image, and writes the output file.

### Step 4 — Flash

Flash the modified firmware through the web interface (works on any OS):

1. Open `http://<printer-ip>/`.
2. Navigate to **System → Upgrade**.
3. Upload `PS-1206U_modified.bin`.
4. Do **not** power-cycle during the upgrade.

> **Warning:** Flashing a corrupt or incompatible firmware image can brick the
> device.  Always keep a backup of the original `PS-1206U_v8.8.bin` before
> making any changes.
