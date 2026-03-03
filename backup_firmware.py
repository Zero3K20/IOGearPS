#!/usr/bin/env python3
"""
backup_firmware.py - Backup firmware from an IOGear GPSU21 / Edimax PS-1206U print server.

Usage:
    python backup_firmware.py --host <device-ip> [--output <output-file>]
                              [--username <username>] [--password <password>]

The script connects to the print server's web interface, retrieves the firmware
image, and saves it locally so you can restore it later if needed.
"""

import argparse
import hashlib
import sys
import urllib.request
import urllib.error
import urllib.parse
import base64
import os


DEFAULT_USERNAME = "admin"
DEFAULT_PASSWORD = ""

# Minimum plausible size for a firmware blob (smaller responses are likely
# error pages or redirects rather than a real firmware image).
MIN_FIRMWARE_SIZE = 1024

# Context window used when extracting a firmware-version hint from HTML.
SNIPPET_PREFIX_LENGTH = 10
SNIPPET_SUFFIX_LENGTH = 60

FIRMWARE_CANDIDATES = [
    "/firmware.bin",
    "/PS06EPS.BIN",
    "/ps06eps.bin",
    "/upgrade/firmware.bin",
    "/firmware/image.bin",
]


def _build_auth_header(username: str, password: str) -> str:
    credentials = base64.b64encode(
        f"{username}:{password}".encode()
    ).decode()
    return f"Basic {credentials}"


def _open_url(url: str, auth_header: str, timeout: int = 15) -> bytes:
    req = urllib.request.Request(url)
    req.add_header("Authorization", auth_header)
    req.add_header(
        "User-Agent",
        "Mozilla/5.0 (compatible; IOGearPS-Backup/1.0)",
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


def get_device_info(host: str, auth_header: str) -> dict:
    """Fetch basic device info from the web interface home page."""
    info = {}
    for path in ("/home.htm", "/index.htm", "/", "/status.htm"):
        url = f"http://{host}{path}"
        try:
            data = _open_url(url, auth_header)
            text = data.decode("latin-1", errors="replace")
            info["page"] = path
            info["content_length"] = len(data)
            # Look for firmware version hints in the HTML
            for keyword in ("firmware", "version", "Firmware", "Version"):
                idx = text.lower().find(keyword.lower())
                if idx != -1:
                    snippet = text[max(0, idx - SNIPPET_PREFIX_LENGTH) : idx + SNIPPET_SUFFIX_LENGTH]
                    info.setdefault("version_hint", snippet.strip())
                    break
            return info
        except urllib.error.HTTPError as exc:
            if exc.code in (401, 403):
                raise PermissionError(
                    f"Authentication failed for {url} (HTTP {exc.code}). "
                    "Check --username and --password."
                ) from exc
        except (urllib.error.URLError, OSError):
            continue
    return info


def download_firmware(
    host: str,
    auth_header: str,
    output_path: str,
) -> bool:
    """
    Attempt to download the firmware image from known paths.

    Returns True if a firmware image was successfully saved, False otherwise.
    """
    for path in FIRMWARE_CANDIDATES:
        url = f"http://{host}{path}"
        print(f"  Trying {url} …", end=" ", flush=True)
        try:
            data = _open_url(url, auth_header)
            if len(data) < MIN_FIRMWARE_SIZE:
                print("response too small, skipping.")
                continue
            with open(output_path, "wb") as fh:
                fh.write(data)
            digest = hashlib.sha256(data).hexdigest()
            print(f"OK ({len(data):,} bytes)")
            print(f"  SHA-256: {digest}")
            return True
        except urllib.error.HTTPError as exc:
            if exc.code in (401, 403):
                raise PermissionError(
                    f"Authentication failed for {url} (HTTP {exc.code}). "
                    "Check --username and --password."
                ) from exc
            print(f"HTTP {exc.code}")
        except (urllib.error.URLError, OSError) as exc:
            print(f"error ({exc})")
    return False


def compare_with_reference(
    backup_path: str, reference_path: str
) -> None:
    """Compare the backed-up firmware with a local reference image."""
    if not os.path.isfile(reference_path):
        return
    with open(backup_path, "rb") as fh:
        backup_digest = hashlib.sha256(fh.read()).hexdigest()
    with open(reference_path, "rb") as fh:
        ref_digest = hashlib.sha256(fh.read()).hexdigest()
    if backup_digest == ref_digest:
        print(
            f"\n✓ Backed-up firmware matches reference file: {reference_path}"
        )
    else:
        print(
            f"\n⚠  Backed-up firmware does NOT match reference file: {reference_path}"
        )
        print(f"   Backup    SHA-256: {backup_digest}")
        print(f"   Reference SHA-256: {ref_digest}")


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description=(
            "Backup firmware from an IOGear GPSU21 / Edimax PS-1206U "
            "print server."
        )
    )
    parser.add_argument(
        "--host",
        required=True,
        metavar="IP_OR_HOSTNAME",
        help="IP address or hostname of the print server (e.g. 192.168.0.1)",
    )
    parser.add_argument(
        "--output",
        default="firmware_backup.bin",
        metavar="FILE",
        help="Output file path for the firmware backup (default: firmware_backup.bin)",
    )
    parser.add_argument(
        "--username",
        default=DEFAULT_USERNAME,
        help=f"Web-interface username (default: {DEFAULT_USERNAME!r})",
    )
    parser.add_argument(
        "--password",
        default=DEFAULT_PASSWORD,
        help="Web-interface password (default: empty string)",
    )
    parser.add_argument(
        "--reference",
        default="PS-1206U_v8.8.bin",
        metavar="FILE",
        help=(
            "Optional local reference firmware image to compare against "
            "(default: PS-1206U_v8.8.bin)"
        ),
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    auth_header = _build_auth_header(args.username, args.password)

    print(f"Connecting to print server at http://{args.host}/ …")
    try:
        info = get_device_info(args.host, auth_header)
    except PermissionError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)

    if info:
        print(f"  Reached device page: {info.get('page', '?')} "
              f"({info.get('content_length', 0):,} bytes)")
        if "version_hint" in info:
            print(f"  Version hint: {info['version_hint']!r}")
    else:
        print(
            "WARNING: Could not reach the device web interface. "
            "Make sure the host is reachable and on the same network.",
            file=sys.stderr,
        )

    print(f"\nSearching for firmware image …")
    try:
        found = download_firmware(args.host, auth_header, args.output)
    except PermissionError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)

    if found:
        print(f"\nFirmware backup saved to: {args.output}")
        compare_with_reference(args.output, args.reference)
        print(
            "\nTo restore this firmware later, use the print server's web "
            "interface\n(Administration → Firmware Upgrade) and upload the "
            "saved .bin file."
        )
    else:
        print(
            "\nCould not download firmware via the web interface.",
            file=sys.stderr,
        )
        print(
            "The firmware image bundled in this repository "
            "(PS-1206U_v8.8.bin) can be used\nas a known-good restore "
            "image for your IOGear GPSU21 / Edimax PS-1206U device.",
            file=sys.stderr,
        )
        sys.exit(1)


if __name__ == "__main__":
    main()
