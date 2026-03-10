#!/usr/bin/env python3
"""
airprint_proxy.py — Reliable AirPrint mDNS proxy for the IOGear GPSU21
                    (and any other IPP/631 print server).

WHY THIS EXISTS
───────────────
The OEM eCos firmware in both the 2017 (MPS56_IOG_GPSU21_20171123) and 2019
(MPS56_90956F_9034_20191119) GPSU21 builds contains the Apple mDNSCore stack
(mDNSPosix.c).  Binary analysis confirms five distinct lock-corruption failure
paths in that implementation.  When any of them triggers, the mDNS thread
stops advertising the printer — AirPrint discovery breaks silently until the
device is power-cycled.

This script runs on *any computer on the same network* and takes over the
mDNS advertising role completely.  The OEM firmware's broken mDNS responder
becomes irrelevant: this proxy answers iOS/macOS AirPrint queries directly and
sends periodic keep-alive announcements so the printer stays visible without
any reboots.

HOW IT WORKS
────────────
The script opens a UDP socket on port 5353 (the mDNS port), joins the mDNS
multicast group (224.0.0.251), and:

  1. Sends three rapid mDNS announcements on startup (RFC 6762 §8.3).
  2. Listens for incoming mDNS queries.  Any query that asks about _ipp._tcp
     triggers an immediate response.
  3. Sends a fresh announcement every --interval seconds regardless of
     incoming queries.

Records advertised (one DNS response packet, 6–7 answer RRs):
  PTR  _services._dns-sd._udp.local  →  _ipp._tcp.local
  PTR  _ipp._tcp.local               →  <name>._ipp._tcp.local
  PTR  _universal._sub._ipp._tcp.local → <name>._ipp._tcp.local
  SRV  <name>._ipp._tcp.local        →  <hostname>.local : <port>
  TXT  <name>._ipp._tcp.local        →  AirPrint key=value pairs
  A    <hostname>.local              →  <ip>

REQUIREMENTS
────────────
  • Python 3.6 or later
  • No third-party libraries — pure standard library only
  • Root / Administrator rights are NOT required on most platforms

USAGE
─────
  python3 tools/airprint_proxy.py --ip 192.168.1.100

  python3 tools/airprint_proxy.py \\
      --ip       192.168.1.100 \\
      --name    "Office Printer" \\
      --hostname gpsu21 \\
      --port     631 \\
      --interval 5

Run this on any always-on machine on the same subnet as the GPSU21
(Raspberry Pi, NAS, desktop PC).  Press Ctrl+C to stop.

CONFLICT WITH SYSTEM AVAHI / BONJOUR
─────────────────────────────────────
If the host already runs Avahi (Linux) or the macOS mDNS responder, they
may already hold port 5353.  In that case:

  • Linux/Avahi: drop the provided Avahi service file into
    /etc/avahi/services/ instead — see tools/gpsu21-airprint.service.
  • macOS: use the dns-sd command (see README) or disable the system
    mDNS responder before running this script.
  • Windows: this script binds with SO_REUSEADDR and should coexist with
    the Bonjour service.  If it fails, stop the "Bonjour Service" in
    Services first and use the script exclusively.
"""

import argparse
import socket
import struct
import time
import sys

# ── mDNS constants ────────────────────────────────────────────────────────────

MDNS_GROUP = "224.0.0.251"
MDNS_PORT  = 5353
MDNS_TTL   = 4500  # seconds — how long clients cache our records


# ── DNS wire-format helpers ───────────────────────────────────────────────────

def _encode_name(name: str) -> bytes:
    """
    Encode a dotted DNS name to RFC 1035 wire format.
    Example: "gpsu21._ipp._tcp.local" →
             b'\\x06gpsu21\\x04_ipp\\x04_tcp\\x05local\\x00'
    """
    out = b""
    for label in name.split("."):
        if label:
            enc = label.encode("utf-8")
            if len(enc) > 63:
                enc = enc[:63]
            out += bytes([len(enc)]) + enc
    out += b"\x00"
    return out


def _encode_txt(*kvs: str) -> bytes:
    """Encode key=value strings as DNS TXT RDATA (each string length-prefixed)."""
    out = b""
    for kv in kvs:
        enc = kv.encode("utf-8")
        if len(enc) > 255:
            enc = enc[:255]
        out += bytes([len(enc)]) + enc
    return out


def _rr(name: str, rtype: int, rdata: bytes,
        ttl: int = MDNS_TTL, cache_flush: bool = False) -> bytes:
    """Build a single DNS resource record in wire format."""
    # mDNS cache-flush bit lives in the high bit of the CLASS field (RFC 6762 §11.3)
    cls = 0x8001 if cache_flush else 0x0001
    return (
        _encode_name(name)
        + struct.pack(">HHI", rtype, cls, ttl)
        + struct.pack(">H", len(rdata))
        + rdata
    )


def _ptr(owner: str, target: str) -> bytes:
    """Build a PTR resource record (no cache-flush — PTR records are shared)."""
    return _rr(owner, 12, _encode_name(target), cache_flush=False)


def _srv(owner: str, port: int, target: str,
         priority: int = 0, weight: int = 0) -> bytes:
    """Build a SRV resource record (unique — cache-flush set)."""
    rdata = struct.pack(">HHH", priority, weight, port) + _encode_name(target)
    return _rr(owner, 33, rdata, cache_flush=True)


def _txt(owner: str, *kvs: str) -> bytes:
    """Build a TXT resource record (unique — cache-flush set)."""
    return _rr(owner, 16, _encode_txt(*kvs), cache_flush=True)


def _a(owner: str, ip: str) -> bytes:
    """Build an A resource record (unique — cache-flush set)."""
    return _rr(owner, 1, socket.inet_aton(ip), cache_flush=True)


# ── Announcement packet builder ───────────────────────────────────────────────

def build_announcement(ip: str, name: str, hostname: str, port: int) -> bytes:
    """
    Build a complete mDNS announcement packet for the GPSU21 IPP service.

    The packet is a DNS response (QR=1, AA=1) with 6 answer records:

      PTR  _services._dns-sd._udp.local  → _ipp._tcp.local
      PTR  _ipp._tcp.local               → <name>._ipp._tcp.local
      PTR  _universal._sub._ipp._tcp.local → <name>._ipp._tcp.local
           ↑ required for AirPrint on iOS (both old and new)
      SRV  <name>._ipp._tcp.local        → <hostname>.local : <port>
      TXT  <name>._ipp._tcp.local        → AirPrint capability TXT records
      A    <hostname>.local              → <ip>
    """
    svc_inst = f"{name}._ipp._tcp.local"
    svc_type = "_ipp._tcp.local"
    sub_type = "_universal._sub._ipp._tcp.local"
    sd_ptr   = "_services._dns-sd._udp.local"
    host     = f"{hostname}.local"
    admin    = f"http://{ip}/"

    answers = [
        # Shared PTR records — NO cache-flush (RFC 6762 §11.3)
        _ptr(sd_ptr,   svc_type),
        _ptr(svc_type, svc_inst),
        _ptr(sub_type, svc_inst),
        # Unique records for this host — cache-flush set
        _srv(svc_inst, port, host),
        _txt(
            svc_inst,
            "txtvers=1",
            "qtotal=1",
            "rp=ipp/print",
            "ty=IOGear GPSU21",
            f"adminurl={admin}",
            "note=",
            "priority=50",
            "product=(GPL Ghostscript)",
            "pdl=application/pdf,image/jpeg,image/png,image/urf,"
            "application/octet-stream",
            "Color=F",
            "Duplex=F",
            "usb_MFG=IOGear",
            "usb_MDL=GPSU21",
            "air=username,password,t2600,pwg",
        ),
        _a(host, ip),
    ]

    header = struct.pack(
        ">HHHHHH",
        0x0000,          # ID  = 0 (mDNS always uses 0)
        0x8400,          # Flags: QR=1 (response), AA=1 (authoritative)
        0x0000,          # QDCOUNT = 0
        len(answers),    # ANCOUNT
        0x0000,          # NSCOUNT = 0
        0x0000,          # ARCOUNT = 0
    )
    return header + b"".join(answers)


# ── Incoming query detection ──────────────────────────────────────────────────

def _is_ipp_query(data: bytes) -> bool:
    """
    Return True if data is an mDNS *query* (QR=0) that contains the
    "_ipp" service-type label.  This is the trigger for an immediate
    response — no need to parse the full DNS message.
    """
    if len(data) < 12:
        return False
    if data[2] & 0x80:      # QR bit set → this is a response, not a query
        return False
    # Scan for the 4-byte label sequence 0x04 '_' 'i' 'p' 'p'
    for i in range(12, len(data) - 4):
        if data[i] == 4 and data[i + 1 : i + 5] == b"_ipp":
            return True
    return False


# ── Socket setup ─────────────────────────────────────────────────────────────

def _make_socket() -> socket.socket:
    """
    Create a UDP socket bound to port 5353 and joined to the mDNS
    multicast group 224.0.0.251.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)

    # SO_REUSEADDR lets us share port 5353 with other processes (e.g. Avahi).
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    # SO_REUSEPORT (Linux/macOS only — absent on Windows).
    if hasattr(socket, "SO_REUSEPORT"):
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except OSError:
            pass

    # Bind to INADDR_ANY on port 5353.
    # Binding to all interfaces is intentional and required for mDNS: the
    # mDNS multicast group (224.0.0.251) must be joined on every network
    # interface so that queries sent to that group are received regardless of
    # which interface they arrive on.  Binding to a specific IP would silently
    # drop queries arriving on other interfaces.
    sock.bind(("", MDNS_PORT))

    # Join the mDNS IPv4 multicast group on all interfaces.
    mreq = struct.pack("4sL", socket.inet_aton(MDNS_GROUP), socket.INADDR_ANY)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

    # TTL=255 is required for mDNS (RFC 6762 §11.4).
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
    # Disable loopback so we do not hear our own announcements.
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)

    # Receive timeout drives the periodic announcement: we wake up every
    # --interval seconds even when no query arrives.  The caller sets the
    # actual value via sock.settimeout() after this function returns.

    return sock


# ── Main loop ─────────────────────────────────────────────────────────────────

def run_proxy(ip: str, name: str, hostname: str,
              port: int, interval: int) -> None:
    """
    Open the mDNS socket, send initial announcements, then loop forever:
    respond to _ipp._tcp queries and send periodic keep-alive announcements.
    """
    dest = (MDNS_GROUP, MDNS_PORT)

    try:
        sock = _make_socket()
    except OSError as exc:
        print(f"[airprint_proxy] ERROR: could not open mDNS socket: {exc}",
              file=sys.stderr)
        print(
            "[airprint_proxy] HINT: if another process already holds port 5353 "
            "(Avahi, macOS mDNSResponder, Bonjour service), see the comment at "
            "the top of this script for alternatives.",
            file=sys.stderr,
        )
        sys.exit(1)

    sock.settimeout(float(interval))

    pkt = build_announcement(ip, name, hostname, port)

    print(f"[airprint_proxy] Advertising '{name}' → {ip}:{port}  "
          f"(mDNS A record: {hostname}.local)")
    print(f"[airprint_proxy] Sending periodic announcements every {interval}s.")
    print("[airprint_proxy] Press Ctrl+C to stop.\n")

    # RFC 6762 §8.3: send three rapid announcements at startup (1 s apart).
    for i in range(3):
        sock.sendto(pkt, dest)
        if i < 2:
            time.sleep(1.0)

    while True:
        try:
            data, _addr = sock.recvfrom(1024)
            if _is_ipp_query(data):
                # Small random-ish delay to reduce collision risk (RFC 6762 §6).
                time.sleep(0.1)
                sock.sendto(pkt, dest)
        except socket.timeout:
            # No query arrived — send the periodic keep-alive announcement.
            sock.sendto(pkt, dest)
        except KeyboardInterrupt:
            print("\n[airprint_proxy] Stopped.")
            break
        except OSError as exc:
            print(f"[airprint_proxy] socket error: {exc}", file=sys.stderr)
            time.sleep(1.0)

    sock.close()


# ── CLI ───────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        prog="airprint_proxy.py",
        description=(
            "Reliable AirPrint mDNS proxy for the IOGear GPSU21 "
            "(replaces the buggy OEM mDNSCore stack without touching the firmware)."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Examples:\n"
            "  python3 tools/airprint_proxy.py --ip 192.168.1.100\n"
            "  python3 tools/airprint_proxy.py --ip 192.168.1.100 "
            "--name 'Office Printer'\n"
        ),
    )
    parser.add_argument(
        "--ip",
        required=True,
        metavar="PRINTER_IP",
        help="IP address of the GPSU21 on your network (required).",
    )
    parser.add_argument(
        "--name",
        default="IOGear GPSU21",
        metavar="NAME",
        help='AirPrint service name shown on iOS/macOS (default: "IOGear GPSU21").',
    )
    parser.add_argument(
        "--hostname",
        default="gpsu21",
        metavar="HOSTNAME",
        help=(
            "mDNS hostname advertised in the A and SRV records, without "
            "the .local suffix (default: gpsu21)."
        ),
    )
    parser.add_argument(
        "--port",
        type=int,
        default=631,
        metavar="PORT",
        help="IPP port on the printer (default: 631).",
    )
    parser.add_argument(
        "--interval",
        type=int,
        default=5,
        metavar="SECONDS",
        help="Seconds between unsolicited keep-alive announcements (default: 5).",
    )
    args = parser.parse_args()

    run_proxy(args.ip, args.name, args.hostname, args.port, args.interval)


if __name__ == "__main__":
    main()
