/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * MCUboot KEYHASH-prefix parser against real imgtool-signed images
 * (fixtures/make_fixtures.sh). The dev prefix is what `imgtool dumpinfo` shows
 * for MCUboot's root-ec-p256.pem; station-manager compares this value against
 * the release key before it flashes, so a wrong (rather than absent) key here
 * would be a real hazard -- every malformed input must yield "absent".
 */
#include "boot_key.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/ztest.h>

namespace {

const uint8_t kDevSigned[] = {
#include "dev-signed.inc"
};
const uint8_t kOtherSigned[] = {
#include "other-signed.inc"
};
const uint8_t kProtSigned[] = {
#include "prot-signed.inc"
};

constexpr const char *kDevPrefix = "e30466f6b8470c1f";
// KEYHASH[0:8] of fixtures/other-key.pem (imgtool dumpinfo other-signed.bin).
constexpr const char *kOtherPrefix = "271b30fab2c5517f";

struct Buf {
  const uint8_t *data;
  size_t len;
  int fail_after; // < 0: never fail; otherwise number of successful reads left
  int reads;
};

int buf_read(void *ctx, size_t off, void *dst, size_t len) {
  auto *b = static_cast<Buf *>(ctx);
  if (b->fail_after >= 0 && b->reads >= b->fail_after) {
    return -EIO;
  }
  b->reads++;
  if (off > b->len || len > b->len - off) {
    return -EINVAL;
  }
  memcpy(dst, b->data + off, len);
  return 0;
}

uint16_t le16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t le32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
void put16(uint8_t *p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) {
    p[i] = static_cast<uint8_t>(v >> (8 * i));
  }
}

// Mutable copy for corruption tests (static: keep the test frames small).
uint8_t s_img[4096];

size_t copy(const uint8_t *src, size_t len) {
  zassert_true(len <= sizeof(s_img));
  memcpy(s_img, src, len);
  return len;
}

// Offset of the unprotected TLV info (0x6907) in a fixture.
size_t tlv_info_off(const uint8_t *img) {
  return le16(img + 8) + le32(img + 12) + le16(img + 10);
}

// Independent walk: offset of the KEYHASH entry header in a fixture.
size_t keyhash_entry_off(const uint8_t *img) {
  size_t info = tlv_info_off(img);
  zassert_equal(le16(img + info), 0x6907);
  size_t end = info + le16(img + info + 2);
  for (size_t off = info + 4; off + 4 <= end; off += 4 + le16(img + off + 2)) {
    if (le16(img + off) == 0x0001) {
      return off;
    }
  }
  zassert_unreachable("fixture has no KEYHASH");
  return 0;
}

bool run(const uint8_t *data, size_t len, size_t size, char *out) {
  Buf b{data, len, -1, 0};
  return mod::boot_key_from_image(buf_read, &b, size, out);
}

void expect_absent(const uint8_t *data, size_t len, size_t size) {
  char out[mod::kBootKeyHexLen + 1];
  memset(out, 'x', sizeof(out));
  zassert_false(run(data, len, size, out));
  zassert_equal(out[0], '\0');
}

} // namespace

ZTEST(boot_key, test_dev_signed_yields_dev_prefix) {
  char out[mod::kBootKeyHexLen + 1];
  zassert_true(run(kDevSigned, sizeof(kDevSigned), sizeof(kDevSigned), out));
  zassert_str_equal(out, kDevPrefix);
  zassert_equal(strlen(out), mod::kBootKeyHexLen);
}

ZTEST(boot_key, test_other_key_yields_other_prefix) {
  char out[mod::kBootKeyHexLen + 1];
  zassert_true(run(kOtherSigned, sizeof(kOtherSigned), sizeof(kOtherSigned), out));
  zassert_str_equal(out, kOtherPrefix);
  zassert_true(strcmp(out, kDevPrefix) != 0);
}

ZTEST(boot_key, test_protected_tlv_area_is_skipped) {
  zassert_not_equal(le16(kProtSigned + 10), 0, "fixture must carry a protected TLV area");
  char out[mod::kBootKeyHexLen + 1];
  zassert_true(run(kProtSigned, sizeof(kProtSigned), sizeof(kProtSigned), out));
  zassert_str_equal(out, kDevPrefix);
}

ZTEST(boot_key, test_image_inside_larger_partition) {
  // A real slot is far bigger than the image; trailing erased flash is ignored.
  static uint8_t slot[8192];
  memset(slot, 0xFF, sizeof(slot));
  memcpy(slot, kDevSigned, sizeof(kDevSigned));
  char out[mod::kBootKeyHexLen + 1];
  zassert_true(run(slot, sizeof(slot), sizeof(slot), out));
  zassert_str_equal(out, kDevPrefix);
}

ZTEST(boot_key, test_erased_header_returns_absent) {
  static uint8_t erased[512];
  memset(erased, 0xFF, sizeof(erased));
  expect_absent(erased, sizeof(erased), sizeof(erased));
}

ZTEST(boot_key, test_truncated_image_returns_absent) {
  expect_absent(kDevSigned, sizeof(kDevSigned), 600);
  expect_absent(kDevSigned, sizeof(kDevSigned), 8);
  expect_absent(kDevSigned, sizeof(kDevSigned), 0);
  // Cut inside the TLV area (after the info, before KEYHASH data ends).
  expect_absent(kDevSigned, sizeof(kDevSigned), keyhash_entry_off(kDevSigned) + 8);
  // Cut one byte short of the declared TLV area end.
  size_t info = tlv_info_off(kDevSigned);
  expect_absent(kDevSigned, sizeof(kDevSigned), info + le16(kDevSigned + info + 2) - 1);
}

ZTEST(boot_key, test_tlv_len_overflow_returns_absent) {
  size_t n = copy(kDevSigned, sizeof(kDevSigned));
  put16(s_img + tlv_info_off(s_img) + 2, 0xFFFF);
  expect_absent(s_img, n, n);
}

ZTEST(boot_key, test_tlv_entry_len_overflow_returns_absent) {
  // An entry whose length runs past the declared TLV area end.
  size_t n = copy(kDevSigned, sizeof(kDevSigned));
  size_t first = tlv_info_off(s_img) + 4;
  put16(s_img + first + 2, 0xFFF0);
  expect_absent(s_img, n, n);
}

ZTEST(boot_key, test_img_size_overflow_returns_absent) {
  // hdr_size + img_size must not wrap into a small, in-bounds offset.
  size_t n = copy(kDevSigned, sizeof(kDevSigned));
  put32(s_img + 12, 0xFFFFFFFFu);
  expect_absent(s_img, n, n);
  expect_absent(s_img, n, SIZE_MAX);
}

ZTEST(boot_key, test_no_keyhash_tlv_returns_absent) {
  size_t n = copy(kDevSigned, sizeof(kDevSigned));
  put16(s_img + keyhash_entry_off(s_img), 0x007F);
  expect_absent(s_img, n, n);
}

ZTEST(boot_key, test_keyhash_wrong_len_returns_absent) {
  size_t n = copy(kDevSigned, sizeof(kDevSigned));
  put16(s_img + keyhash_entry_off(s_img) + 2, 0x0010);
  expect_absent(s_img, n, n);
}

ZTEST(boot_key, test_wrong_magic_returns_absent) {
  size_t n = copy(kDevSigned, sizeof(kDevSigned));
  s_img[0] ^= 0x01;
  expect_absent(s_img, n, n);
}

ZTEST(boot_key, test_wrong_tlv_info_magic_returns_absent) {
  size_t n = copy(kDevSigned, sizeof(kDevSigned));
  put16(s_img + tlv_info_off(s_img), 0x1234);
  expect_absent(s_img, n, n);
}

ZTEST(boot_key, test_inconsistent_protect_tlv_size_returns_absent) {
  // Header claims a protected area but the TLV start holds the unprotected info.
  size_t n = copy(kDevSigned, sizeof(kDevSigned));
  put16(s_img + 10, 0x000C);
  expect_absent(s_img, n, n);
  // Protected area present but header size disagrees with its total_len.
  n = copy(kProtSigned, sizeof(kProtSigned));
  put16(s_img + 10, static_cast<uint16_t>(le16(s_img + 10) + 4));
  expect_absent(s_img, n, n);
}

ZTEST(boot_key, test_read_error_returns_absent) {
  char out[mod::kBootKeyHexLen + 1];
  // Fail the very first read, then every later read in turn: no partial result
  // may ever leak out, until enough reads succeed to complete the walk.
  bool succeeded = false;
  for (int n = 0; n < 64 && !succeeded; n++) {
    Buf b{kProtSigned, sizeof(kProtSigned), n, 0};
    memset(out, 'x', sizeof(out));
    succeeded = mod::boot_key_from_image(buf_read, &b, sizeof(kProtSigned), out);
    if (!succeeded) {
      zassert_equal(out[0], '\0', "partial output after read failure #%d", n);
    }
  }
  zassert_true(succeeded);
  zassert_str_equal(out, kDevPrefix);
}

ZTEST(boot_key, test_null_read_fn_returns_absent) {
  char out[mod::kBootKeyHexLen + 1];
  memset(out, 'x', sizeof(out));
  zassert_false(mod::boot_key_from_image(nullptr, nullptr, sizeof(kDevSigned), out));
  zassert_equal(out[0], '\0');
}

ZTEST_SUITE(boot_key, NULL, NULL, NULL, NULL, NULL);
