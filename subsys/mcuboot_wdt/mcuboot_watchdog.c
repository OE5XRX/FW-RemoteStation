/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * fm_board MCUboot watchdog setup — overrides MCUboot's weak
 * mcuboot_watchdog_setup() (bootloader/mcuboot/boot/zephyr/watchdog.c).
 *
 * MCUboot arms the IWDG (aliased watchdog0) before it validates, swaps and
 * jumps, so an application that hangs before it takes the watchdog over —
 * e.g. stuck in the clock driver's HSERDY wait because the crystal does not
 * start — is reset and, being an unconfirmed trial image, reverted. The app
 * re-programs the IWDG to its own task_wdt period once its boot-confirm gate
 * starts (app/src/boot_confirm/boot_confirm_fm.cpp); MCUboot feeds it during
 * long swap/hash loops (CONFIG_BOOT_WATCHDOG_FEED, MCUboot's default feed is
 * kept).
 *
 * The only difference to MCUboot's default is WDT_OPT_PAUSE_HALTED_BY_DBG:
 * MCUboot passes 0, so a core halted by SWD (pyocd flash/debug while the
 * board sits in MCUboot or in a pre-takeover hang) would keep the IWDG
 * counting and reset the chip mid-flash. The app's task_wdt already arms the
 * IWDG with this option; this keeps the bootloader consistent with it.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(mcuboot, CONFIG_MCUBOOT_LOG_LEVEL);

BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(watchdog0)), "fm_board MCUboot needs watchdog0 -> &iwdg");

void mcuboot_watchdog_setup(void);

void mcuboot_watchdog_setup(void)
{
	const struct device *wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
	const struct wdt_timeout_cfg cfg = {
		.window = {.min = 0U, .max = CONFIG_BOOT_WATCHDOG_TIMEOUT_MS},
		.callback = NULL,
		.flags = WDT_FLAG_RESET_SOC,
	};
	int rc;

	if (!device_is_ready(wdt)) {
		LOG_ERR("Watchdog not ready");
		return;
	}

	/* MCUboot's weak feed uses channel 0; the IWDG has a single channel. */
	rc = wdt_install_timeout(wdt, &cfg);
	if (rc < 0) {
		LOG_ERR("Watchdog install timeout failed: %d", rc);
		return;
	}

	rc = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
	if (rc != 0) {
		LOG_ERR("Watchdog setup failed: %d", rc);
	}
}
