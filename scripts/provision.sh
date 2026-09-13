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
# SC2015: intentional — usage() runs (and exits) if ANY arg is missing; no else-branch bug.
# shellcheck disable=SC2015
[ -n "$MCUBOOT" ] && [ -n "$APP" ] && [ -n "$MTYPE" ] && [ -n "$VERSION" ] || usage
[ -f "$MCUBOOT" ] || { echo "no such file: $MCUBOOT" >&2; exit 1; }
[ -f "$APP" ] || { echo "no such file: $APP" >&2; exit 1; }

here="$(cd "$(dirname "$0")" && pwd)"

# Refuse to chip-erase + flash a non-production image: verify the app is signed
# with the committed production public key BEFORE any destructive operation.
pub="$here/../release/signing/oe5xrx-fw-public.pem"
if command -v imgtool >/dev/null 2>&1 && [ -f "$pub" ]; then
  echo "== Verifying production signature ($APP) =="
  imgtool verify -k "$pub" "$APP" || {
    echo "ERROR: $APP is NOT production-signed (imgtool verify failed) — refusing to flash." >&2
    exit 1
  }
else
  echo "WARN: skipping production-signature verify (imgtool or $pub missing) — flashing unverified image." >&2
fi

echo "== Flashing bootloader ($MCUBOOT) with chip erase =="
pyocd flash --target "$TARGET" --erase chip "$MCUBOOT"

echo "== Flashing signed app ($APP) at $SLOT0 =="
pyocd flash --target "$TARGET" --base-address "$SLOT0" "$APP"

echo "== Reading UID =="
uid="$(python3 "$here/read_uid.py" --target "$TARGET")"
echo "UID=$uid"

out="provision-${uid}.json"
ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
python3 -c 'import json,sys; keys=["uid","module_type","pyocd_target","firmware_version","signed_app","mcuboot_hex","provisioned_at"]; print(json.dumps(dict(zip(keys,sys.argv[1:])), indent=2))' "$uid" "$MTYPE" "$TARGET" "$VERSION" "$(basename "$APP")" "$(basename "$MCUBOOT")" "$ts" > "$out"
echo "Wrote $out — hand this to station-manager module registration (Teilbereich A)."
