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

ZTEST_SUITE(device_uid, NULL, NULL, NULL, NULL, NULL);
