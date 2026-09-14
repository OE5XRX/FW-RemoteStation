/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Cross-check the runtime UID formatter against scripts/read_uid.py::format_uid.
 * The canonical vector and zero-padding case mirror scripts/tests/test_read_uid.py
 * byte-for-byte: a regression here means station-manager (A) cannot match a
 * bench-provisioned module to its heartbeat.
 */
#include "device_uid.h"

#include <zephyr/ztest.h>

ZTEST(device_uid, test_format_matches_read_uid_py) {
  const uint32_t words[3] = {0x11223344u, 0x55667788u, 0x99AABBCCu};
  char out[25];
  mod_format_uid(words, out);
  zassert_str_equal(out, "99AABBCC5566778811223344");
}

ZTEST(device_uid, test_format_zero_padding) {
  const uint32_t words[3] = {0x1u, 0x0u, 0x0u};
  char out[25];
  mod_format_uid(words, out);
  zassert_str_equal(out, "000000000000000000000001");
}

ZTEST(device_uid, test_format_bytes_matches_hwinfo_order) {
  // Zephyr STM32 hwinfo (3-word families) returns buf = be32(Word2), be32(Word1),
  // be32(Word0). For the canonical read_uid.py vector [W0=0x11223344,
  // W1=0x55667788, W2=0x99AABBCC] that is these 12 bytes; emitting them verbatim
  // as hex must reproduce the same canonical string as mod_format_uid.
  const uint8_t buf[12] = {0x99, 0xAA, 0xBB, 0xCC, 0x55, 0x66, 0x77, 0x88, 0x11, 0x22, 0x33, 0x44};
  char out[25];
  mod_format_uid_bytes(buf, out);
  zassert_str_equal(out, "99AABBCC5566778811223344");
}

ZTEST_SUITE(device_uid, NULL, NULL, NULL, NULL, NULL);
