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

#include <fcntl.h>
#include <unistd.h>
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

ZTEST(device_uid, test_uid_is_valid_hex24) {
  zassert_true(mod_uid_is_valid_hex24("99AABBCC5566778811223344"));
  zassert_false(mod_uid_is_valid_hex24("99aabbcc5566778811223344")); // lower-case
  zassert_false(mod_uid_is_valid_hex24("99AABBCC55667788112233"));   // 22 chars
  zassert_false(mod_uid_is_valid_hex24("99AABBCC556677881122334G")); // non-hex
  zassert_false(mod_uid_is_valid_hex24(NULL));
}

ZTEST(device_uid, test_sim_uid_persist_roundtrip) {
  // Prove the synthetic UID survives a simulated restart: the second call reads
  // back exactly what the first call persisted to the file (this is what happens
  // across a native_sim process restart), and a corrupt file regenerates a valid
  // UID rather than propagating garbage.
  const char *path = "test_uid_roundtrip.txt";
  unlink(path);

  char a[25];
  char b[25];
  mod_uid_sim_load_or_make(path, a); // generates + persists
  zassert_true(mod_uid_is_valid_hex24(a));
  mod_uid_sim_load_or_make(path, b); // reloads from file ("restart")
  zassert_str_equal(a, b);

  int fd = open(path, O_WRONLY | O_TRUNC, 0644);
  zassert_true(fd >= 0);
  const char junk[] = "not-valid-hex";
  ssize_t wr = write(fd, junk, sizeof(junk) - 1);
  close(fd);
  zassert_equal(wr, (ssize_t)(sizeof(junk) - 1));

  char c[25];
  mod_uid_sim_load_or_make(path, c); // corrupt -> regenerate
  zassert_true(mod_uid_is_valid_hex24(c));
  unlink(path);
}

ZTEST_SUITE(device_uid, NULL, NULL, NULL, NULL, NULL);
