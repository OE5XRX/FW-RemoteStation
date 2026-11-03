/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Module::describe emission of the optional identity.boot_key: present (right
 * after uid_source) when non-empty, omitted entirely when nullptr or "" -- the
 * server treats an absent key as "unknown", never an empty string as a key.
 */
#include <oe5xrx/module/iface.h>
#include <span>
#include <string.h>
#include <zephyr/ztest.h>

namespace {

char s_buf[512];

const char *render(const mod::Identity &id) {
  mod::Module m{id, "fm", std::span<mod::Capability *const>{}};
  mod::JsonWriter w(s_buf, sizeof(s_buf));
  m.describe(w);
  zassert_false(w.truncated());
  return w.c_str();
}

constexpr const char *kPrefix = R"({"schema":2,"module":"fm","identity":{"type":"fm_transceiver","model":"SA818-V",)"
                                R"("version":"26.11.03-00","variant":"vhf","uid":"0123456789ABCDEF01234567","uid_source":"stm32_uid")";

} // namespace

ZTEST(boot_key_describe, test_boot_key_emitted_after_uid_source) {
  const mod::Identity id{"fm_transceiver", "SA818-V", "26.11.03-00", "vhf", "0123456789ABCDEF01234567", "stm32_uid", "e30466f6b8470c1f"};
  char expected[512];
  snprintf(expected, sizeof(expected), "%s%s", kPrefix, R"(,"boot_key":"e30466f6b8470c1f"},"capabilities":[]})");
  zassert_str_equal(render(id), expected);
}

ZTEST(boot_key_describe, test_null_boot_key_omitted) {
  const mod::Identity id{"fm_transceiver", "SA818-V", "26.11.03-00", "vhf", "0123456789ABCDEF01234567", "stm32_uid"};
  char expected[512];
  snprintf(expected, sizeof(expected), "%s%s", kPrefix, R"(},"capabilities":[]})");
  zassert_str_equal(render(id), expected);
  zassert_is_null(strstr(s_buf, "boot_key"));
}

ZTEST(boot_key_describe, test_empty_boot_key_omitted) {
  const mod::Identity id{"fm_transceiver", "SA818-V", "26.11.03-00", "vhf", "0123456789ABCDEF01234567", "stm32_uid", ""};
  char expected[512];
  snprintf(expected, sizeof(expected), "%s%s", kPrefix, R"(},"capabilities":[]})");
  zassert_str_equal(render(id), expected);
  zassert_is_null(strstr(s_buf, "boot_key"));
}

ZTEST_SUITE(boot_key_describe, NULL, NULL, NULL, NULL, NULL);
