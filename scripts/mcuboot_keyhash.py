#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""MCUboot KEYHASH prefix of a signed image, and MODULE-DESCRIBE console parsing.

keyhash_prefix() is the Python twin of the firmware parser in
subsys/module/boot_key.cpp: same TLV walk, same strictness (any doubt -> None),
so the HIL gate can compare the firmware's identity.boot_key against the image
it actually flashed. Pure stdlib; unit-tested in scripts/tests.

Image layout (little-endian, bootutil/image.h):
  0x00 u32 ih_magic 0x96F3B83D, 0x08 u16 ih_hdr_size,
  0x0A u16 ih_protect_tlv_size (0 if absent), 0x0C u32 ih_img_size
  tlv = ih_hdr_size + ih_img_size
  [protected]   info{u16 0x6908, u16 total_len == ih_protect_tlv_size} + entries
  [unprotected] info{u16 0x6907, u16 total_len (incl. info)} + entries
  entry: u16 type, u16 len, len bytes data; KEYHASH = type 0x0001, len 32
"""

from __future__ import annotations

import json
import re
import struct

IMAGE_MAGIC = 0x96F3B83D
TLV_INFO_MAGIC = 0x6907
TLV_PROT_INFO_MAGIC = 0x6908
TLV_KEYHASH = 0x0001
KEYHASH_LEN = 32
BOOT_KEY_HEX_LEN = 16
_TLV_HDR_LEN = 4
_HEADER_READ_LEN = 16

DESCRIBE_TOKEN = "MODULE-DESCRIBE "
_ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
BOOT_KEY_RE = re.compile(r"[0-9a-f]{16}")


def _fits(off: int, length: int, size: int) -> bool:
    """True iff [off, off + length) lies within [0, size)."""
    return 0 <= length <= size and 0 <= off <= size - length


def _pair(data: bytes, off: int) -> tuple[int, int] | None:
    """Read a TLV info/entry header {u16 a, u16 b} at off, bounds-checked."""
    if not _fits(off, _TLV_HDR_LEN, len(data)):
        return None
    return struct.unpack_from("<HH", data, off)


def keyhash_prefix(data: bytes) -> str | None:
    """First 16 lowercase hex chars of the KEYHASH TLV, or None on any failure."""
    size = len(data)
    if not _fits(0, _HEADER_READ_LEN, size):
        return None
    magic32, _load, hdr_size, protect_size, img_size = struct.unpack_from("<IIHHI", data, 0)
    if magic32 != IMAGE_MAGIC:
        return None

    off = hdr_size + img_size
    pair = _pair(data, off)
    if pair is None:
        return None
    magic, total = pair
    if protect_size != 0 or magic == TLV_PROT_INFO_MAGIC:
        # Protected area must be present and agree with the header (as MCUboot
        # itself requires); skip it wholesale.
        if magic != TLV_PROT_INFO_MAGIC or total != protect_size or total < _TLV_HDR_LEN:
            return None
        off += total
        pair = _pair(data, off)
        if pair is None:
            return None
        magic, total = pair
    if magic != TLV_INFO_MAGIC or total < _TLV_HDR_LEN or not _fits(off, total, size):
        return None

    end = off + total
    off += _TLV_HDR_LEN
    while _fits(off, _TLV_HDR_LEN, end):
        tlv_type, length = struct.unpack_from("<HH", data, off)
        off += _TLV_HDR_LEN
        if not _fits(off, length, end):
            return None
        if tlv_type == TLV_KEYHASH:
            if length != KEYHASH_LEN:
                return None
            return data[off : off + BOOT_KEY_HEX_LEN // 2].hex()
        off += length
    return None


def keyhash_prefix_file(path: str) -> str | None:
    """keyhash_prefix() of the file at path (None if unreadable or not an image)."""
    try:
        with open(path, "rb") as f:
            return keyhash_prefix(f.read())
    except OSError:
        return None


def parse_describe(console_text: str) -> dict | None:
    """json-load the payload of the first parseable `MODULE-DESCRIBE {json}` line.

    Tolerates shell echo, prompts, ANSI escapes and CR line endings. Lines whose
    payload is not a JSON object (e.g. cut off mid-read) are skipped.
    """
    for raw in _ANSI.sub("", console_text).replace("\r", "\n").split("\n"):
        i = raw.find(DESCRIBE_TOKEN)
        if i == -1:
            continue
        try:
            payload = json.loads(raw[i + len(DESCRIBE_TOKEN) :].strip())
        except ValueError:
            continue
        if isinstance(payload, dict):
            return payload
    return None


if __name__ == "__main__":  # pragma: no cover
    import argparse
    import sys

    ap = argparse.ArgumentParser(description="Print the MCUboot KEYHASH prefix (16 hex) of a signed image")
    ap.add_argument("image", help="MCUboot-signed .bin (e.g. build/app/zephyr/zephyr.signed.bin)")
    prefix = keyhash_prefix_file(ap.parse_args().image)
    if prefix is None:
        print("no KEYHASH found", file=sys.stderr)
        sys.exit(1)
    print(prefix)
