#!/bin/sh
# get_hp1020_firmware.sh — Download the HP LaserJet 1020 EZ-USB firmware
#
# The HP LaserJet 1015/1020/1022 require a small Cypress EZ-USB firmware
# blob to be uploaded at every power-on before the printer will enumerate
# as a USB Printer Class device.  The blob is distributed by the foo2zjs
# project and has been publicly hosted on GitHub for many years.
#
# This script downloads the blob and either:
#   a) Uploads it directly to a running GPSU21 print server (--upload), or
#   b) Saves it to a local file for use with the firmware build system, or
#   c) Saves it to both.
#
# Usage:
#   ./scripts/get_hp1020_firmware.sh [OPTIONS]
#
# Options:
#   --upload <IP>    Upload firmware to the print server at <IP>
#                    via POST /api/upload_printer_fw
#   --save <FILE>    Save downloaded blob to <FILE> (default: sihp1020.dl)
#   --help           Show this help
#
# Example — download and upload in one step:
#   ./scripts/get_hp1020_firmware.sh --upload 192.168.1.100
#
# Example — download and save for baking into the firmware binary:
#   ./scripts/get_hp1020_firmware.sh --save /tmp/sihp1020.dl
#   make -C firmware HP1020_FW=/tmp/sihp1020.dl ...
#
# NOTE ON COPYRIGHT:
#   The sihp1020.dl firmware binary was authored by HP Inc. and is
#   HP-proprietary.  It has been distributed alongside the foo2zjs open-source
#   driver package for many years.  By downloading this file you accept full
#   responsibility for complying with HP's end-user license terms for the HP
#   LaserJet 1020 printer.  This script only automates the download; it does
#   not grant any additional rights.
#
# Sources (same binary, multiple mirrors):
#   https://github.com/inveneo/hub-linux-ubuntu/raw/master/install/overlay/usr/share/foo2zjs/firmware/sihp1020.dl
#   /usr/share/hplip/data/firmware/hp_laserjet_1020.fw  (if HPLIP is installed)
#   /usr/share/foo2zjs/firmware/sihp1020.img             (if foo2zjs is installed)

set -e

# ── Defaults ────────────────────────────────────────────────────────────────
SAVE_FILE=""
UPLOAD_IP=""
DL_URL="https://raw.githubusercontent.com/inveneo/hub-linux-ubuntu/master/install/overlay/usr/share/foo2zjs/firmware/sihp1020.dl"
DEFAULT_SAVE="sihp1020.dl"

# ── Argument parsing ─────────────────────────────────────────────────────────
while [ $# -gt 0 ]; do
    case "$1" in
        --upload)
            shift
            UPLOAD_IP="$1"
            shift
            ;;
        --save)
            shift
            SAVE_FILE="$1"
            shift
            ;;
        --help|-h)
            sed -n '2,/^set -e/{ s/^# \{0,1\}//; /^set -e/d; p }' "$0"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            echo "Run '$0 --help' for usage." >&2
            exit 1
            ;;
    esac
done

# ── Require at least one action ──────────────────────────────────────────────
if [ -z "$SAVE_FILE" ] && [ -z "$UPLOAD_IP" ]; then
    SAVE_FILE="$DEFAULT_SAVE"
fi

# ── Check for required tools ─────────────────────────────────────────────────
need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Error: '$1' is required but not found in PATH." >&2
        exit 1
    fi
}
need_cmd curl

# ── Copyright notice ─────────────────────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════════════════════════╗"
echo "║  HP LaserJet 1020 firmware download                                  ║"
echo "╠══════════════════════════════════════════════════════════════════════╣"
echo "║  The firmware file (sihp1020.dl) is HP-proprietary software.         ║"
echo "║  By proceeding you accept responsibility for complying with HP's      ║"
echo "║  license terms for the HP LaserJet 1020 printer.                      ║"
echo "╚══════════════════════════════════════════════════════════════════════╝"
echo ""

# ── Download to a temp file ───────────────────────────────────────────────────
TMP_FILE="$(mktemp /tmp/sihp1020_XXXXXX.dl)"
trap 'rm -f "$TMP_FILE"' EXIT INT TERM

echo "Downloading from:"
echo "  $DL_URL"
echo ""

if ! curl -fsSL --progress-bar -o "$TMP_FILE" "$DL_URL"; then
    echo "" >&2
    echo "Download failed." >&2
    echo "" >&2
    echo "If the GitHub URL is unreachable, try one of these alternatives:" >&2
    echo "  sudo apt install hplip && cp /usr/share/hplip/data/firmware/hp_laserjet_1020.fw ." >&2
    echo "  sudo apt install foo2zjs && cp /usr/share/foo2zjs/firmware/sihp1020.img ." >&2
    exit 1
fi

# Sanity check: file must be at least 1 KB
BYTES="$(wc -c < "$TMP_FILE")"
if [ "$BYTES" -lt 1024 ]; then
    echo "Error: downloaded file is too small ($BYTES bytes) — possible 404 page." >&2
    exit 1
fi
echo "Downloaded: $BYTES bytes"
echo ""

# ── Save to file ─────────────────────────────────────────────────────────────
if [ -n "$SAVE_FILE" ]; then
    cp "$TMP_FILE" "$SAVE_FILE"
    echo "Saved to: $SAVE_FILE"
    echo ""
    echo "To upload to a running print server:"
    echo "  curl -X POST http://<ip>/api/upload_printer_fw --data-binary @$SAVE_FILE"
    echo ""
    echo "To bake into the firmware binary (so it works out-of-the-box):"
    echo "  make -C firmware HP1020_FW=$SAVE_FILE FREERTOS_DIR=... LWIP_DIR=..."
    echo ""
fi

# ── Upload to print server ────────────────────────────────────────────────────
if [ -n "$UPLOAD_IP" ]; then
    URL="http://${UPLOAD_IP}/api/upload_printer_fw"
    echo "Uploading to print server at $UPLOAD_IP ..."
    RESPONSE="$(curl -s -X POST "$URL" \
        -H "Content-Type: application/octet-stream" \
        --data-binary "@$TMP_FILE")"
    echo "Response: $RESPONSE"
    echo ""

    # Check for {"ok":true,...}
    case "$RESPONSE" in
        *'"ok":true'*)
            echo "✓ Firmware uploaded successfully."
            echo "  The print server will now automatically upload firmware to"
            echo "  the HP LaserJet 1020 whenever the printer is powered on."
            ;;
        *)
            echo "✗ Upload may have failed — check the response above." >&2
            exit 1
            ;;
    esac
fi
