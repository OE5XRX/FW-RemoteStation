#!/usr/bin/env bash
# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Regenerate the imgtool-signed MCUboot fixtures for tests/unit_boot_key.
#
#   root-ec-p256.pem  MCUboot's well-known *dev* key, copied verbatim from the
#                     mcuboot tree (bootloader/mcuboot/root-ec-p256.pem, sha256
#                     1734d7ee76344fbf2b1731f49baa5e1ced9bea56b66fe84703b1a492e0a13614).
#                     It is published in the mcuboot repo and is NOT a secret;
#                     imgtool sign needs the private part, so it is checked in.
#                     KEYHASH prefix: e30466f6b8470c1f.
#   other-key.pem     Throwaway test key, generated once with
#                     `imgtool keygen -t ecdsa-p256`. Never used for real images.
#                     Created only if missing, so re-running keeps the recorded
#                     prefix in src/main.cpp (kOtherPrefix) valid.
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

[ -f other-key.pem ] || $IMG keygen -t ecdsa-p256 -k other-key.pem

payload=$(mktemp)
trap 'rm -f "$payload"' EXIT
head -c 2048 /dev/zero | tr '\0' '\125' >"$payload"

common=(--header-size 0x200 --pad-header --align 4 --version 1.2.3 --slot-size 0x20000)
$IMG sign --key root-ec-p256.pem "${common[@]}" "$payload" dev-signed.bin
$IMG sign --key other-key.pem "${common[@]}" "$payload" other-signed.bin
$IMG sign --key root-ec-p256.pem "${common[@]}" --security-counter 7 "$payload" prot-signed.bin
