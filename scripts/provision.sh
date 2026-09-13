#!/usr/bin/env bash
# One-time bench provisioning of an fm_board: flash the production MCUboot +
# first signed app over SWD, then read the STM32 UID and emit provision-<uid>.json.
# HARDWARE REQUIRED: an ST-Link/CMSIS-DAP probe + the target board attached.
set -euo pipefail

usage() {
  echo "usage: $0 --mcuboot <mcuboot.hex> --app <signed.bin> --type <module_type> --version <YY.MM.DD-NN> [--target <pyocd_target>] [--slot0 <addr>]" >&2
  exit 2
}

TARGET="stm32u575citx"
SLOT0="0x08020000"   # slot0 base (see mcuboot-dfu-secure-bringup spec / fm_board.dts)
MCUBOOT="" APP="" MTYPE="" VERSION=""
while [ $# -gt 0 ]; do
  case "$1" in
    --mcuboot) MCUBOOT="$2"; shift 2;;
    --app) APP="$2"; shift 2;;
    --type) MTYPE="$2"; shift 2;;
    --version) VERSION="$2"; shift 2;;
    --target) TARGET="$2"; shift 2;;
    --slot0) SLOT0="$2"; shift 2;;
    *) usage;;
  esac
done
[ -n "$MCUBOOT" ] && [ -n "$APP" ] && [ -n "$MTYPE" ] && [ -n "$VERSION" ] || usage
[ -f "$MCUBOOT" ] || { echo "no such file: $MCUBOOT" >&2; exit 1; }
[ -f "$APP" ] || { echo "no such file: $APP" >&2; exit 1; }

echo "== Flashing bootloader ($MCUBOOT) with chip erase =="
pyocd flash --target "$TARGET" --erase chip "$MCUBOOT"

echo "== Flashing signed app ($APP) at $SLOT0 =="
pyocd flash --target "$TARGET" --base-address "$SLOT0" "$APP"

echo "== Reading UID =="
here="$(cd "$(dirname "$0")" && pwd)"
uid="$(python3 "$here/read_uid.py" --target "$TARGET")"
echo "UID=$uid"

out="provision-${uid}.json"
ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
cat > "$out" <<EOF
{
  "uid": "${uid}",
  "module_type": "${MTYPE}",
  "pyocd_target": "${TARGET}",
  "firmware_version": "${VERSION}",
  "signed_app": "$(basename "$APP")",
  "mcuboot_hex": "$(basename "$MCUBOOT")",
  "provisioned_at": "${ts}"
}
EOF
echo "Wrote $out — hand this to station-manager module registration (Teilbereich A)."
