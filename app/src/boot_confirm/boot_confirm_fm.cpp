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
 *   - SA818 AT handshake (sa818_at_connect round-trip over UART). Omitted when
 *     CONFIG_FM_BOOT_CONFIRM_SKIP_SA818 is set (bench/HIL builds without a
 *     working radio); production builds always keep this criterion.
 *
 * IWDG / task_wdt: task_wdt is initialised with the hardware IWDG as its
 * fallback.  The gate thread registers one channel with a period of
 * boot::timing::WDT_PERIOD_MS and feeds it after every probe sweep (GateHooks
 * feed hook).  The period is derived from the SA818 AT timeout (see
 * boot_confirm_timing.h): a probe that merely waits out an unanswered
 * AT+DMOCONNECT is slow, not hung, and must not trip the watchdog — otherwise
 * a trial boot without a responding radio reverts after ~2.5 s instead of at
 * the 30-s deadline.  Only a genuinely hung gate thread lets the channel
 * expire; wdt_cb then lets the IWDG bite (reset cause WATCHDOG) with a
 * software-reboot backstop.  On the next boot MCUboot detects the image was
 * never confirmed and reverts to the previous slot.  The software deadline in
 * run_health_gate (h_reboot) is the normal revert path: it fires when the gate
 * is schedulable but the probes stay unhealthy past the deadline, and logs
 * which criteria failed before resetting (reset cause SOFTWARE).
 *
 * Diagnostics: every boot logs (and clears) the hardware reset cause, so a
 * revert is attributable from the log of the next boot of an image carrying
 * this code: SOFTWARE = health-gate deadline (or any sys_reboot), WATCHDOG =
 * stalled gate thread / IWDG.
 *
 * task_wdt is compiled in only when CONFIG_TASK_WDT is set (prod/sysbuild
 * build).  In the bare build CONFIG_TASK_WDT is not set, so the watchdog block
 * compiles out; this is safe because in bare CONFIG_BOOTLOADER_MCUBOOT is also
 * not set, so h_confd() returns true and the gate exits immediately via the
 * AlreadyConfirmed path without needing watchdog protection.
 *
 * Confirm-failure handling: boot_write_img_confirmed() can transiently fail
 * (e.g. flash busy).  The confirm hook returns the rc; health_gate.cpp treats
 * a non-zero return as "not yet confirmed" and resets the dwell streak so the
 * next healthy period retries the write before the deadline fires.
 *
 * @copyright Copyright (c) 2026 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#include "boot_confirm_timing.h"
#include "health_gate.h"

#include <atomic>
#include <errno.h>
#include <sa818/sa818.h>
#include <sa818/sa818_at.h>
#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/usb/usbd.h>

#if defined(CONFIG_TASK_WDT)
#include <zephyr/task_wdt/task_wdt.h>
#endif

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include <zephyr/dfu/mcuboot.h>
#endif

LOG_MODULE_REGISTER(boot_confirm, LOG_LEVEL_INF);

/* Timing budget (deadline/dwell/poll + task_wdt period) lives in
 * boot_confirm_timing.h, shared with tests/boot_confirm. */
using namespace boot::timing;

namespace {

struct Env {
  const struct device *sa818;
  std::atomic<bool> usb_configured{false}; /* written from USBD callback; read from gate thread */
  int sa818_last_rc = SA818_OK;            /* last probe_sa818 result (gate thread only); -ENODEV = not ready */
};

Env g_env;

#if defined(CONFIG_TASK_WDT)
static int g_wdt_channel = -1; /* task_wdt channel id; -1 = not initialised */
#endif

/* ---- probes ---------------------------------------------------------------- */

bool probe_usb(void *c) {
  return static_cast<Env *>(c)->usb_configured.load();
}

bool probe_shell(void *c) {
  /* Shell transport ready == USB composite configured (CDC-ACM is part of it)
   * AND the shell backend is initialised by SYS_INIT at boot.
   * This returns a compile-time constant (IS_ENABLED(CONFIG_SHELL)) and
   * contributes no runtime signal — USB-configured probe covers the transport.
   * See file-level comment for rationale. */
  (void)c;
  return IS_ENABLED(CONFIG_SHELL);
}

#if !IS_ENABLED(CONFIG_FM_BOOT_CONFIRM_SKIP_SA818)
bool probe_sa818(void *c) {
  Env *env = static_cast<Env *>(c);
  if (!device_is_ready(env->sa818)) {
    env->sa818_last_rc = -ENODEV;
    return false;
  }
  /* AT+DMOCONNECT round-trip; [[nodiscard]] — result must not be discarded.
   * Blocks up to SA818_AT_TIMEOUT_MS (plus a wait on the driver's AT mutex)
   * when the radio does not answer — accounted for in WDT_PERIOD_MS. */
  enum sa818_result rc = sa818_at_connect(env->sa818);
  env->sa818_last_rc = rc;
  return rc == SA818_OK;
}
#endif /* !CONFIG_FM_BOOT_CONFIRM_SKIP_SA818 */

/* ---- hooks ----------------------------------------------------------------- */

int64_t h_now(void *) {
  return k_uptime_get();
}

void h_sleep(void *, int64_t ms) {
  k_msleep(static_cast<int32_t>(ms));
}

void h_feed(void *) {
#if defined(CONFIG_TASK_WDT)
  /* Called by run_health_gate after every completed probe sweep to prove the
   * gate thread is alive.  No-op if the channel could not be added. */
  if (g_wdt_channel >= 0) {
    (void)task_wdt_feed(g_wdt_channel);
  }
#endif
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

void h_reboot(void *c) {
  const Env *env = static_cast<const Env *>(c);
  LOG_WRN("health-gate deadline (%d ms) exceeded — rebooting for MCUboot revert", static_cast<int>(GATE_DEADLINE_MS));
  LOG_WRN("health-gate state: usb_configured=%d shell=%d", env->usb_configured.load() ? 1 : 0, IS_ENABLED(CONFIG_SHELL) ? 1 : 0);
#if !IS_ENABLED(CONFIG_FM_BOOT_CONFIRM_SKIP_SA818)
  LOG_WRN("health-gate state: sa818 last AT+DMOCONNECT rc=%d", env->sa818_last_rc);
#endif
  /* Thread context: drain the deferred log buffer and give the CDC-ACM
   * transport a moment, so the revert is not silent.  Bounded: the task_wdt
   * channel is still armed and fed just before this hook ran. */
  log_flush();
  k_msleep(REBOOT_LOG_DRAIN_MS);
  sys_reboot(SYS_REBOOT_COLD);
  k_sleep(K_FOREVER); /* prevent thread from touching state during reset window */
}

/* ---- task_wdt callback ----------------------------------------------------- */

#if defined(CONFIG_TASK_WDT)
void wdt_backstop_expiry(struct k_timer *) {
  sys_reboot(SYS_REBOOT_COLD);
}

struct k_timer g_wdt_backstop; /* initialised in gate_thread before the channel is added */

static void wdt_cb(int channel_id, void *user_data) {
  (void)channel_id;
  (void)user_data;
  /* Called from the task_wdt kernel-timer (ISR) context when the channel was
   * not fed within WDT_PERIOD_MS — i.e. the gate thread is genuinely hung, not
   * just waiting out an AT timeout (that is inside the budget).
   *
   * After a channel with a callback expires, task_wdt stops feeding the
   * hardware IWDG, so with the IWDG armed it bites within
   * CONFIG_TASK_WDT_MIN_TIMEOUT + CONFIG_TASK_WDT_HW_FALLBACK_DELAY.  Letting
   * it do so (instead of sys_reboot() here) leaves RESET_WATCHDOG as the reset
   * cause, which the next boot logs — distinguishable from the deadline
   * revert (RESET_SOFTWARE).  If the IWDG is not armed (task_wdt_init with a
   * NULL hw_wdt), the one-shot backstop timer reboots instead.  Either way the
   * image is unconfirmed and MCUboot reverts on the next boot.  The log line
   * is best-effort: ISR context cannot flush the deferred log. */
  LOG_ERR("task_wdt timeout on boot-confirm gate thread — resetting for MCUboot revert");
  k_timer_start(&g_wdt_backstop, K_MSEC(WDT_REBOOT_BACKSTOP_MS), K_NO_WAIT);
}
#endif

/* ---- reset-cause diagnostics ----------------------------------------------- */

void log_reset_cause() {
  uint32_t cause = 0;
  if (hwinfo_get_reset_cause(&cause) != 0) {
    return; /* not supported on this target */
  }
  (void)hwinfo_clear_reset_cause(); /* flags are sticky across resets; report each reset once */
  if (cause & RESET_WATCHDOG) {
    LOG_WRN("reset cause 0x%08x: WATCHDOG (IWDG — e.g. hung boot-confirm gate thread)", cause);
  } else if (cause & RESET_SOFTWARE) {
    LOG_INF("reset cause 0x%08x: SOFTWARE (sys_reboot — e.g. health-gate deadline revert, DFU, shell reboot)", cause);
  } else {
    LOG_INF("reset cause 0x%08x", cause);
  }
}

/* ---- thread ---------------------------------------------------------------- */

K_THREAD_STACK_DEFINE(g_stack, 2048);
struct k_thread g_thread;

void gate_thread(void *, void *, void *) {
  log_reset_cause();

#if defined(CONFIG_TASK_WDT)
  /* Initialise task_wdt with the hw IWDG as fallback so it actually resets
   * the chip if we stall.  The IWDG node must have status = "okay" in the
   * board DTS (fm_board.dts already does this). */
  const struct device *hw_wdt = DEVICE_DT_GET(DT_NODELABEL(iwdg));
  int init_rc = task_wdt_init(device_is_ready(hw_wdt) ? hw_wdt : NULL);
  if (init_rc != 0) {
    LOG_ERR("task_wdt_init rc=%d; HW watchdog NOT armed", init_rc);
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
    /* On a trial boot the watchdog is the only backstop against a hung probe
     * blocking the gate thread indefinitely.  Without it the image would stay
     * unconfirmed with no deterministic revert path.  Reboot now so MCUboot
     * reverts to the previous slot rather than leaving the device stranded. */
    if (!boot_is_img_confirmed()) {
      LOG_ERR("trial boot — rebooting for deterministic MCUboot revert");
      sys_reboot(SYS_REBOOT_COLD);
    }
#endif
    /* Already-confirmed image or bare build: no watchdog revert risk; keep running. */
  } else {
    k_timer_init(&g_wdt_backstop, wdt_backstop_expiry, NULL);
    g_wdt_channel = task_wdt_add(WDT_PERIOD_MS, wdt_cb, NULL);
    if (g_wdt_channel < 0) {
      LOG_ERR("task_wdt_add rc=%d; HW watchdog NOT armed", g_wdt_channel);
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
      if (!boot_is_img_confirmed()) {
        LOG_ERR("trial boot — rebooting for deterministic MCUboot revert");
        sys_reboot(SYS_REBOOT_COLD);
      }
#endif
      g_wdt_channel = -1;
    }
  }
#endif /* CONFIG_TASK_WDT */

  using namespace boot;
  static HealthCriterion crit[] = {
      {"usb", probe_usb, &g_env},
      {"shell", probe_shell, &g_env},
#if !IS_ENABLED(CONFIG_FM_BOOT_CONFIRM_SKIP_SA818)
      /* SA818 AT handshake — omitted on a bench build (see
       * CONFIG_FM_BOOT_CONFIRM_SKIP_SA818) that has no working radio. */
      {"sa818", probe_sa818, &g_env},
#endif
  };
  const GateHooks hooks{h_now, h_sleep, h_confd, h_confirm, h_reboot, h_feed, &g_env};
  const GateConfig cfg{GATE_DEADLINE_MS, GATE_DWELL_MS, GATE_POLL_MS};

  GateOutcome outcome = boot::run_health_gate(cfg, hooks, crit, ARRAY_SIZE(crit));

#if defined(CONFIG_TASK_WDT)
  /* Delete the watchdog channel — image is confirmed (or reboot was called). */
  if (g_wdt_channel >= 0) {
    (void)task_wdt_delete(g_wdt_channel);
    g_wdt_channel = -1;
  }
#endif

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
  g_env.usb_configured.store(true);
}

extern "C" void boot_confirm_fm_start(const struct device *sa818) {
  g_env.sa818 = sa818;
  g_env.usb_configured.store(false);
  k_thread_create(&g_thread, g_stack, K_THREAD_STACK_SIZEOF(g_stack), gate_thread, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
  k_thread_name_set(&g_thread, "boot_confirm");
}
