/**
 * @file boot_confirm_fm.cpp
 * @brief fm_board wiring of the MCUboot health-gate confirm thread.
 *
 * Runs boot::run_health_gate() with real probes:
 *   - USB composite configured (USBD_MSG_CONFIGURATION event from the USBD
 *     message callback, stored in g_env.usb_configured).
 *   - Shell transport ready (compile-time; CDC-ACM is part of the USB
 *     composite — when USB is configured the shell backend is active. This is
 *     intentionally NOT a DTR/terminal-open runtime check: that would gate
 *     confirm on an operator plugging in a terminal, which is fragile on
 *     unattended stations. The constant IS_ENABLED(CONFIG_SHELL) is here to
 *     make the criterion explicit and searchable, not as a runtime signal).
 *   - SA818 AT handshake (sa818_at_connect round-trip over UART).
 *
 * IWDG / task_wdt: task_wdt is initialised with the hardware IWDG as its
 * fallback.  The gate thread registers one channel with a period of
 * GATE_POLL_MS * 4, and feeds it on every probe sweep.  If the gate thread
 * stalls (e.g. blocked in sa818_at_connect) the task_wdt fires its callback
 * which logs an error; the hw IWDG then triggers a cold reboot shortly after.
 *
 * Confirm-failure handling: boot_write_img_confirmed() can transiently fail
 * (e.g. flash busy).  The confirm hook returns the rc; health_gate.cpp treats
 * a non-zero return as "not yet confirmed" and resets the dwell streak so the
 * next healthy period retries the write before the deadline fires.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#include "health_gate.h"

#include <sa818/sa818.h>
#include <sa818/sa818_at.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/usb/usbd.h>

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include <zephyr/dfu/mcuboot.h>
#endif

LOG_MODULE_REGISTER(boot_confirm, LOG_LEVEL_INF);

/* Timing constants (milliseconds) */
static constexpr int64_t GATE_DEADLINE_MS = 30000; /* revert if not healthy within 30 s */
static constexpr int64_t GATE_DWELL_MS = 3000;     /* require 3 s continuous health */
static constexpr int32_t GATE_POLL_MS = 250;       /* probe sweep interval */

/* task_wdt channel period: 4x poll so one missed sweep is not fatal */
static constexpr uint32_t WDT_PERIOD_MS = static_cast<uint32_t>(GATE_POLL_MS) * 4u;

namespace {

struct Env {
  const struct device *sa818;
  volatile bool usb_configured; /* set from USBD message callback (ISR context) */
};

Env g_env;
static int g_wdt_channel = -1; /* task_wdt channel id; -1 = not initialised */

/* ---- probes ---------------------------------------------------------------- */

bool probe_usb(void *c) {
  return static_cast<Env *>(c)->usb_configured;
}

bool probe_shell(void *c) {
  /* Shell transport ready == USB composite configured (CDC-ACM is part of it)
   * AND the shell backend is initialised by SYS_INIT at boot.
   * This is a compile-time constant — see file-level comment for rationale. */
  (void)c;
  return IS_ENABLED(CONFIG_SHELL);
}

bool probe_sa818(void *c) {
  const struct device *d = static_cast<Env *>(c)->sa818;
  if (!device_is_ready(d)) {
    return false;
  }
  /* AT+DMOCONNECT round-trip; [[nodiscard]] — result must not be discarded. */
  enum sa818_result rc = sa818_at_connect(d);
  return rc == SA818_OK;
}

/* ---- hooks ----------------------------------------------------------------- */

int64_t h_now(void *) {
  return k_uptime_get();
}

void h_sleep(void *, int64_t ms) {
  /* Feed the task_wdt on every sleep (i.e., every poll interval) to prove the
   * gate thread is alive.  If g_wdt_channel is not yet initialised (race at
   * startup) the feed is a no-op. */
  if (g_wdt_channel >= 0) {
    (void)task_wdt_feed(g_wdt_channel);
  }
  k_msleep(static_cast<int32_t>(ms));
}

bool h_confd(void *) {
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
  return boot_is_img_confirmed();
#else
  return true; /* bare build: nothing to confirm */
#endif
}

int h_confirm(void *) {
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
  int rc = boot_write_img_confirmed();
  if (rc != 0) {
    LOG_ERR("boot_write_img_confirmed failed (rc=%d); will retry", rc);
  } else {
    LOG_INF("image confirmed");
  }
  return rc;
#else
  return 0;
#endif
}

void h_reboot(void *) {
  LOG_WRN("health-gate deadline exceeded — rebooting for MCUboot revert");
  sys_reboot(SYS_REBOOT_COLD);
}

/* ---- task_wdt callback ----------------------------------------------------- */

static void wdt_cb(int channel_id, void *user_data) {
  (void)channel_id;
  (void)user_data;
  /* Called from the task_wdt timer ISR when the channel has not been fed in
   * time.  Log and let the hw IWDG trigger the actual reboot. */
  LOG_ERR("task_wdt timeout on boot-confirm gate thread — hw IWDG will reboot");
}

/* ---- thread ---------------------------------------------------------------- */

K_THREAD_STACK_DEFINE(g_stack, 2048);
struct k_thread g_thread;

void gate_thread(void *, void *, void *) {
  /* Initialise task_wdt with the hw IWDG as fallback so it actually resets
   * the chip if we stall.  The IWDG node must have status = "okay" in the
   * board DTS (fm_board.dts already does this). */
  const struct device *hw_wdt = DEVICE_DT_GET(DT_NODELABEL(iwdg));
  int init_rc = task_wdt_init(device_is_ready(hw_wdt) ? hw_wdt : NULL);
  if (init_rc != 0) {
    /* RESIDUAL RISK: with task_wdt_init failed, the hardware IWDG is NOT armed.
     * If a probe (e.g. sa818_at_connect) then hangs, there is no watchdog to
     * reset the chip; only the gate's best-effort software deadline applies,
     * and that deadline itself cannot fire while the thread is blocked inside a
     * hung probe. The image can therefore stay unconfirmed AND un-reverted. We
     * do NOT abort/reboot here (that would be its own failure mode); we surface
     * the loss of hardware protection at ERROR severity so it is not silent. */
    LOG_ERR("task_wdt_init rc=%d; HW watchdog NOT armed — no reset backstop if a "
            "probe hangs, only the best-effort software deadline applies",
            init_rc);
  } else {
    g_wdt_channel = task_wdt_add(WDT_PERIOD_MS, wdt_cb, NULL);
    if (g_wdt_channel < 0) {
      LOG_ERR("task_wdt_add rc=%d; HW watchdog NOT armed — no reset backstop if a "
              "probe hangs, only the best-effort software deadline applies",
              g_wdt_channel);
      g_wdt_channel = -1;
    }
  }

  using namespace boot;
  static HealthCriterion crit[] = {
      {"usb", probe_usb, &g_env},
      {"shell", probe_shell, &g_env},
      {"sa818", probe_sa818, &g_env},
  };
  const GateHooks hooks{h_now, h_sleep, h_confd, h_confirm, h_reboot, nullptr};
  const GateConfig cfg{GATE_DEADLINE_MS, GATE_DWELL_MS, GATE_POLL_MS};

  GateOutcome outcome = boot::run_health_gate(cfg, hooks, crit, ARRAY_SIZE(crit));

  /* Delete the watchdog channel — image is confirmed (or reboot was called). */
  if (g_wdt_channel >= 0) {
    (void)task_wdt_delete(g_wdt_channel);
    g_wdt_channel = -1;
  }

  switch (outcome) {
  case GateOutcome::AlreadyConfirmed:
    LOG_INF("boot-confirm: image was already confirmed");
    break;
  case GateOutcome::Confirmed:
    LOG_INF("boot-confirm: image confirmed successfully");
    break;
  case GateOutcome::RevertRebooted:
    /* h_reboot() called sys_reboot(); we should not reach here */
    LOG_ERR("boot-confirm: revert reboot returned (unexpected)");
    break;
  }
}

} // namespace

extern "C" void boot_confirm_fm_usb_configured(void) {
  g_env.usb_configured = true;
}

extern "C" void boot_confirm_fm_start(const struct device *sa818) {
  g_env.sa818 = sa818;
  g_env.usb_configured = false;
  k_thread_create(&g_thread, g_stack, K_THREAD_STACK_SIZEOF(g_stack), gate_thread, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
  k_thread_name_set(&g_thread, "boot_confirm");
}
