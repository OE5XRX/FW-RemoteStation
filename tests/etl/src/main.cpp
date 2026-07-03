/*
 * Copyright (c) 2025 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Proof that a real ETL type links and works on native_sim.
 */

#include <etl/string.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(etl_smoke, NULL, NULL, NULL, NULL, NULL);

ZTEST(etl_smoke, test_string_append_compare) {
  etl::string<32> s("OE5");
  s.append("XRX");

  zassert_equal(s.size(), 6U, "unexpected size %u", (unsigned int)s.size());
  zassert_true(s == "OE5XRX", "content mismatch");
  zassert_equal(s.capacity(), 32U, "unexpected capacity %u", (unsigned int)s.capacity());
  zassert_false(s.full(), "string should not be full");
}
