#!/usr/bin/env bash
# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Regenerate the imgtool-signed MCUboot fixtures for tests/unit_boot_key.
#
# No key material is checked in (keeps secret scanners quiet and keys out of a
# repo that also handles a real production signing key):
#   dev key    MCUboot's well-known dev key, read from the west workspace
#              (bootloader/mcuboot/root-ec-p256.pem, sha256
#              1734d7ee76344fbf2b1731f49baa5e1ced9bea56b66fe84703b1a492e0a13614;
#              override with $MCUBOOT_DEV_KEY). KEYHASH prefix: e30466f6b8470c1f.
#   other key  Throwaway ECDSA-P256 key generated into a temp dir on every run
#              and discarded. Its KEYHASH prefix changes per run, so after
#              regenerating, update kOtherPrefix in src/main.cpp and
#              OTHER_PREFIX in scripts/tests/test_mcuboot_keyhash.py with the
#              value this script prints.
#
# Outputs (unpadded: header + payload + TLVs only, no --pad):
#   dev-signed.bin    dev key
#   other-signed.bin  other key
#   prot-signed.bin   dev key + --security-counter (=> protected TLV area 0x6908)
#
# ECDSA signatures are randomized, so the .bin bytes change on every run, but the
# KEYHASH (hash of the public key) does not. Verify with `imgtool dumpinfo <bin>`.
set -euo pipefail
cd "$(dirname "$0")"

IMG=${IMGTOOL:-"uvx --from imgtool==2.5.0 imgtool"}
DEV_KEY=${MCUBOOT_DEV_KEY:-"${ZEPHYR_BASE:-../../../../zephyr}/../bootloader/mcuboot/root-ec-p256.pem"}
[ -f "$DEV_KEY" ] || { echo "MCUboot dev key not found: $DEV_KEY (set MCUBOOT_DEV_KEY)" >&2; exit 1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
payload="$tmp/payload.bin"
other_key="$tmp/other-key.pem"
$IMG keygen -t ecdsa-p256 -k "$other_key"
head -c 2048 /dev/zero | tr '\0' '\125' >"$payload"

common=(--header-size 0x200 --pad-header --align 4 --version 1.2.3 --slot-size 0x20000)
$IMG sign --key "$DEV_KEY" "${common[@]}" "$payload" dev-signed.bin
$IMG sign --key "$other_key" "${common[@]}" "$payload" other-signed.bin
$IMG sign --key "$DEV_KEY" "${common[@]}" --security-counter 7 "$payload" prot-signed.bin

echo "other-signed.bin KEYHASH prefix (update kOtherPrefix / OTHER_PREFIX):"
python3 ../../../scripts/mcuboot_keyhash.py other-signed.bin
