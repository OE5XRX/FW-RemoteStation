/**
 * @file boot_confirm_timing.h
 * @brief Timing budget of the fm_board health-gate confirm thread.
 *
 * Shared by boot_confirm_fm.cpp and tests/boot_confirm so the watchdog period
 * the firmware arms and the one the tests check against are the same numbers.
 *
 * The task_wdt channel guarding the gate thread is fed once per loop
 * iteration (after each probe sweep). The longest legitimate gap between two
 * feeds is therefore one worst-case probe sweep plus one poll sleep; the
 * channel period must be strictly longer than that, or a merely *slow* probe
 * (no SA818 answer -> full AT timeout) is mistaken for a hung thread and the
 * trial image is reverted within seconds instead of at GATE_DEADLINE_MS.
 *
 * @copyright Copyright (c) 2026 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#pragma once

#include <cstdint>
#include <sa818/sa818_at.h> /* SA818_AT_TIMEOUT_MS */

namespace boot::timing {

inline constexpr int64_t GATE_DEADLINE_MS = 30000; /* revert if not healthy within 30 s */
inline constexpr int64_t GATE_DWELL_MS = 3000;     /* require 3 s continuous health */
inline constexpr int64_t GATE_POLL_MS = 250;       /* probe sweep interval */

/* Worst-case blocking time of one probe sweep. usb/shell probes are
 * non-blocking; probe_sa818 calls sa818_at_connect(), which may first wait on
 * the driver's AT mutex behind one in-flight command from another thread
 * (e.g. the shell; <= one AT timeout) and then block for its own
 * AT+DMOCONNECT up to the AT timeout. */
inline constexpr int64_t PROBE_SWEEP_WORST_MS = 2 * static_cast<int64_t>(SA818_AT_TIMEOUT_MS);

/* Headroom on top of the worst case for scheduling jitter (gate thread runs at
 * the lowest application priority) and log/USB work. */
inline constexpr int64_t WDT_MARGIN_MS = 1000;

/* Longest legitimate gap between two feeds of the gate's task_wdt channel. */
inline constexpr int64_t MAX_FEED_GAP_MS = PROBE_SWEEP_WORST_MS + GATE_POLL_MS;

inline constexpr uint32_t WDT_PERIOD_MS = static_cast<uint32_t>(MAX_FEED_GAP_MS + WDT_MARGIN_MS);

static_assert(WDT_PERIOD_MS > MAX_FEED_GAP_MS, "task_wdt period must exceed one worst-case probe sweep + poll, else a slow SA818 reverts the trial image");
static_assert(WDT_PERIOD_MS > static_cast<uint32_t>(SA818_AT_TIMEOUT_MS), "task_wdt period must exceed the SA818 AT timeout");
static_assert(WDT_PERIOD_MS < GATE_DEADLINE_MS, "task_wdt must still catch a hung gate thread well before the confirm deadline");

/* After logging the revert reason, give the deferred log thread and the
 * CDC-ACM transport this long to push the lines out before resetting. */
inline constexpr int32_t REBOOT_LOG_DRAIN_MS = 200;

/* wdt_cb backstop: if the hardware IWDG is not armed (task_wdt fell back to a
 * pure software timer) nothing else resets the chip after a channel timeout,
 * so a one-shot timer reboots after this delay. With the IWDG armed it bites
 * first (~CONFIG_TASK_WDT_MIN_TIMEOUT + CONFIG_TASK_WDT_HW_FALLBACK_DELAY). */
inline constexpr int32_t WDT_REBOOT_BACKSTOP_MS = 1000;

} // namespace boot::timing
