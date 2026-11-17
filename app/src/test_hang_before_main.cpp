/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * CONFIG_FM_TEST_HANG_BEFORE_MAIN: bench-only image that never reaches main().
 * Stands in for an HSE crystal that does not start (the clock driver would spin
 * on HSERDY at PRE_KERNEL_1); only the IWDG MCUboot armed can get the board out
 * of here, and MCUboot then reverts the unconfirmed trial image.
 */
#include <zephyr/arch/cpu.h>
#include <zephyr/init.h>

static int test_hang_before_main(void) {
  for (;;) {
    arch_nop();
  }
  return 0;
}

SYS_INIT(test_hang_before_main, PRE_KERNEL_2, 0);
