#pragma once

#include <cstddef>
#include <cstdint>

namespace boot {

// Pollable readiness predicate for one subsystem. Returns true once alive.
using HealthProbe = bool (*)(void *ctx);

struct HealthCriterion {
  const char *name;
  HealthProbe probe;
  void *ctx;
};

// Injectable side effects so the state machine is testable without hardware.
struct GateHooks {
  int64_t (*now_ms)(void *ctx);
  void (*sleep_ms)(void *ctx, int64_t ms);
  bool (*already_confirmed)(void *ctx); // wraps boot_is_img_confirmed()
  int (*confirm)(void *ctx);            // wraps boot_write_img_confirmed(); 0 = ok
  void (*reboot)(void *ctx);            // wraps sys_reboot(SYS_REBOOT_COLD)
  void *ctx;
};

struct GateConfig {
  int64_t deadline_ms;      // give up (reboot->revert) after this
  int64_t dwell_ms;         // require continuous health this long before confirming
  int64_t poll_interval_ms; // spacing between probe sweeps
};

enum class GateOutcome { AlreadyConfirmed, Confirmed, RevertRebooted };

// Runs to a terminal outcome. Pure logic: all time/effects go through hooks.
GateOutcome run_health_gate(const GateConfig &cfg, const GateHooks &hooks, const HealthCriterion *criteria, size_t n_criteria);

} // namespace boot
