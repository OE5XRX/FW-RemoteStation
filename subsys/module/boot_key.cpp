/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * MCUboot image layout (little-endian, bootutil/image.h):
 *
 *   0x00 u32 ih_magic            0x96F3B83D
 *   0x08 u16 ih_hdr_size
 *   0x0A u16 ih_protect_tlv_size (incl. its 4-byte info), 0 if absent
 *   0x0C u32 ih_img_size
 *
 *   tlv = ih_hdr_size + ih_img_size
 *   [protected area]   info{u16 0x6908, u16 total_len == ih_protect_tlv_size} + entries
 *   [unprotected area] info{u16 0x6907, u16 total_len (incl. info)} + entries
 *   entry: u16 type, u16 len, len bytes data
 *
 * KEYHASH (type 0x0001, len 32) lives in the unprotected area. Every offset is
 * checked against @p size with non-wrapping arithmetic; any doubt -> absent.
 */
#include "boot_key.h"

#include <stdint.h>

namespace mod {
namespace {

constexpr uint32_t kImageMagic = 0x96F3B83Du;
constexpr uint16_t kTlvInfoMagic = 0x6907;
constexpr uint16_t kTlvProtInfoMagic = 0x6908;
constexpr uint16_t kTlvKeyHash = 0x0001;
constexpr uint16_t kKeyHashLen = 32;
constexpr size_t kHeaderReadLen = 16;
constexpr size_t kTlvHdrLen = 4;
constexpr size_t kPrefixBytes = kBootKeyHexLen / 2;

uint16_t le16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t le32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/** True iff [off, off + len) lies within [0, size), without overflow. */
constexpr bool fits(size_t off, size_t len, size_t size) {
  return len <= size && off <= size - len;
}

/** Non-wrapping a + b; false on overflow. */
constexpr bool add(size_t a, size_t b, size_t &res) {
  if (b > SIZE_MAX - a) {
    return false;
  }
  res = a + b;
  return true;
}

/** Bounds-checked read: [off, off + len) must lie inside the image. */
bool read_at(ImageReadFn read, void *ctx, size_t size, size_t off, void *dst, size_t len) {
  return fits(off, len, size) && read(ctx, off, dst, len) == 0;
}

/** Read a TLV info/entry header {u16 a, u16 b} at @p off. */
bool read_pair(ImageReadFn read, void *ctx, size_t size, size_t off, uint16_t &a, uint16_t &b) {
  uint8_t raw[kTlvHdrLen];
  if (!read_at(read, ctx, size, off, raw, sizeof(raw))) {
    return false;
  }
  a = le16(raw);
  b = le16(raw + 2);
  return true;
}

bool find_keyhash_prefix(ImageReadFn read, void *ctx, size_t size, uint8_t prefix[kPrefixBytes]) {
  uint8_t hdr[kHeaderReadLen];
  if (!read_at(read, ctx, size, 0, hdr, sizeof(hdr)) || le32(hdr) != kImageMagic) {
    return false;
  }
  const uint16_t hdr_size = le16(hdr + 8);
  const uint16_t protect_size = le16(hdr + 10);
  const uint32_t img_size = le32(hdr + 12);

  size_t off = 0;
  if (!add(hdr_size, img_size, off)) {
    return false;
  }

  uint16_t magic = 0;
  uint16_t total = 0;
  if (!read_pair(read, ctx, size, off, magic, total)) {
    return false;
  }
  if (protect_size != 0 || magic == kTlvProtInfoMagic) {
    // Protected area: must be present and agree with the header (as MCUboot
    // itself requires); skip it wholesale.
    if (magic != kTlvProtInfoMagic || total != protect_size || total < kTlvHdrLen) {
      return false;
    }
    if (!add(off, total, off) || !read_pair(read, ctx, size, off, magic, total)) {
      return false;
    }
  }
  if (magic != kTlvInfoMagic || total < kTlvHdrLen || !fits(off, total, size)) {
    return false;
  }

  const size_t end = off + total; // fits() above: no wrap, end <= size.
  off += kTlvHdrLen;
  // Each iteration advances by >= kTlvHdrLen, so the walk is bounded by total.
  while (fits(off, kTlvHdrLen, end)) {
    uint16_t type = 0;
    uint16_t len = 0;
    if (!read_pair(read, ctx, size, off, type, len)) {
      return false;
    }
    off += kTlvHdrLen;
    if (!fits(off, len, end)) {
      return false;
    }
    if (type == kTlvKeyHash) {
      return len == kKeyHashLen && read_at(read, ctx, size, off, prefix, kPrefixBytes);
    }
    off += len;
  }
  return false;
}

} // namespace

bool boot_key_from_image(ImageReadFn read, void *ctx, size_t size, char *out) {
  if (out == nullptr) {
    return false;
  }
  out[0] = '\0';
  uint8_t prefix[kPrefixBytes];
  if (read == nullptr || !find_keyhash_prefix(read, ctx, size, prefix)) {
    return false;
  }
  static constexpr char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < kPrefixBytes; i++) {
    out[2 * i] = kHex[prefix[i] >> 4];
    out[2 * i + 1] = kHex[prefix[i] & 0x0F];
  }
  out[kBootKeyHexLen] = '\0';
  return true;
}

} // namespace mod
