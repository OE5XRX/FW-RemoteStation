/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * fm_board system-clock guard + `sysclk` shell command.
 *
 * The app image runs PLL1 from the 8 MHz HSE crystal (app/boards/
 * fm_board.overlay) because every audio sample clock (TIM6 ADC trigger,
 * TIM7 DAC trigger) is a fixed divider of SYSCLK; on the uncalibrated MSIS
 * the UAC2 stream ran ~0.65 % fast. The build asserts pin that devicetree
 * contract, `sysclk` shows what the RCC actually runs on (the clock driver
 * could in principle leave PLL1 on the source MCUboot handed over).
 */
#include <errno.h>
#include <stm32_ll_pwr.h>
#include <stm32_ll_rcc.h>
#include <zephyr/devicetree.h>
#include <zephyr/shell/shell.h>

BUILD_ASSERT(DT_SAME_NODE(DT_CLOCKS_CTLR(DT_NODELABEL(pll1)), DT_NODELABEL(clk_hse)),
             "fm_board app must run PLL1 from the HSE crystal (app/boards/fm_board.overlay)");
BUILD_ASSERT(DT_PROP(DT_NODELABEL(clk_hse), clock_frequency) == 8000000, "fm_board HSE crystal Y201 is 8 MHz");
BUILD_ASSERT(DT_PROP(DT_NODELABEL(rcc), clock_frequency) == 160000000, "audio TIM6/TIM7 8 kHz dividers assume a 160 MHz SYSCLK");

namespace {

const char *sysclk_name(uint32_t sws) {
  switch (sws) {
  case LL_RCC_SYS_CLKSOURCE_STATUS_MSIS:
    return "MSIS";
  case LL_RCC_SYS_CLKSOURCE_STATUS_HSI:
    return "HSI";
  case LL_RCC_SYS_CLKSOURCE_STATUS_HSE:
    return "HSE";
  case LL_RCC_SYS_CLKSOURCE_STATUS_PLL1:
    return "PLL1";
  default:
    return "?";
  }
}

const char *pll1_src_name(uint32_t src) {
  switch (src) {
  case LL_RCC_PLL1SOURCE_MSIS:
    return "MSIS";
  case LL_RCC_PLL1SOURCE_HSI:
    return "HSI";
  case LL_RCC_PLL1SOURCE_HSE:
    return "HSE";
  default:
    return "none";
  }
}

int cmd_sysclk(const struct shell *sh, size_t argc, char **argv) {
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  const uint32_t sws = LL_RCC_GetSysClkSource();
  const uint32_t pll1_src = LL_RCC_PLL1_GetMainSource();
  const bool ok = sws == LL_RCC_SYS_CLKSOURCE_STATUS_PLL1 && pll1_src == LL_RCC_PLL1SOURCE_HSE && LL_RCC_HSE_IsReady() != 0U;
  shell_print(sh, "SYSCLK %s pll1=%s hse_ready=%u epod_boost=%u hclk=%u %s", sysclk_name(sws), pll1_src_name(pll1_src), LL_RCC_HSE_IsReady(),
              LL_PWR_IsEnabledEPODBooster(), static_cast<unsigned int>(SystemCoreClock), ok ? "OK" : "NOT-ON-HSE");
  return ok ? 0 : -EIO;
}

} // namespace

SHELL_CMD_REGISTER(sysclk, NULL, "Show the active system clock source (expected: PLL1 from the 8 MHz HSE crystal)", cmd_sysclk);
