#include "health_gate.h"

namespace boot {

static bool all_healthy(const HealthCriterion *c, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (!c[i].probe(c[i].ctx)) {
      return false;
    }
  }
  return true;
}

GateOutcome run_health_gate(const GateConfig &cfg, const GateHooks &hooks, const HealthCriterion *criteria, size_t n_criteria) {
  if (hooks.already_confirmed(hooks.ctx)) {
    return GateOutcome::AlreadyConfirmed;
  }

  const int64_t start = hooks.now_ms(hooks.ctx);
  int64_t healthy_since = -1; // -1 == not currently in a healthy streak

  for (;;) {
    const int64_t now = hooks.now_ms(hooks.ctx);

    if (all_healthy(criteria, n_criteria)) {
      if (healthy_since < 0) {
        healthy_since = now;
      }
      if (now - healthy_since >= cfg.dwell_ms) {
        hooks.confirm(hooks.ctx);
        return GateOutcome::Confirmed;
      }
    } else {
      healthy_since = -1; // streak broken -> restart dwell
    }

    if (now - start >= cfg.deadline_ms) {
      hooks.reboot(hooks.ctx);
      return GateOutcome::RevertRebooted;
    }

    hooks.sleep_ms(hooks.ctx, cfg.poll_interval_ms);
  }
}

} // namespace boot
