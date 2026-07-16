#include "health_gate.h"

#include <zephyr/ztest.h>

using namespace boot;

// --- Fake environment -------------------------------------------------------
struct FakeEnv {
  int64_t now = 0;
  bool confirmed_flag = false; // starts unconfirmed (trial boot)
  int confirm_calls = 0;
  int reboot_calls = 0;
};

static int64_t f_now(void *c) {
  return static_cast<FakeEnv *>(c)->now;
}
static void f_sleep(void *c, int64_t ms) {
  static_cast<FakeEnv *>(c)->now += ms;
}
static bool f_confd(void *c) {
  return static_cast<FakeEnv *>(c)->confirmed_flag;
}
static int f_confirm(void *c) {
  static_cast<FakeEnv *>(c)->confirm_calls++;
  return 0;
}
static void f_reboot(void *c) {
  static_cast<FakeEnv *>(c)->reboot_calls++;
}

static GateHooks make_hooks(FakeEnv &e) {
  return GateHooks{f_now, f_sleep, f_confd, f_confirm, f_reboot, &e};
}
static const GateConfig CFG{/*deadline*/ 30000, /*dwell*/ 3000, /*poll*/ 250};

// Scripted probe: healthy once env.now >= threshold.
struct Thresh {
  FakeEnv *e;
  int64_t at;
};
static bool probe_after(void *c) {
  auto *t = static_cast<Thresh *>(c);
  return t->e->now >= t->at;
}

ZTEST(health_gate, test_already_confirmed_is_noop) {
  FakeEnv e;
  e.confirmed_flag = true;
  auto out = run_health_gate(CFG, make_hooks(e), nullptr, 0);
  zassert_equal(out, GateOutcome::AlreadyConfirmed);
  zassert_equal(e.confirm_calls, 0);
  zassert_equal(e.reboot_calls, 0);
}

ZTEST(health_gate, test_all_healthy_confirms_after_dwell) {
  FakeEnv e;
  Thresh t{&e, 0}; // healthy immediately
  HealthCriterion crit{"x", probe_after, &t};
  auto out = run_health_gate(CFG, make_hooks(e), &crit, 1);
  zassert_equal(out, GateOutcome::Confirmed);
  zassert_equal(e.confirm_calls, 1);
  zassert_equal(e.reboot_calls, 0);
  zassert_true(e.now >= CFG.dwell_ms); // waited the dwell
  zassert_true(e.now < CFG.deadline_ms);
}

ZTEST(health_gate, test_late_but_within_deadline_confirms) {
  FakeEnv e;
  Thresh t{&e, 20000}; // becomes healthy at 20s (radio power-up latency)
  HealthCriterion crit{"sa818", probe_after, &t};
  auto out = run_health_gate(CFG, make_hooks(e), &crit, 1);
  zassert_equal(out, GateOutcome::Confirmed);
  zassert_true(e.now >= 20000 + CFG.dwell_ms);
}

ZTEST(health_gate, test_never_healthy_reverts_at_deadline) {
  FakeEnv e;
  Thresh t{&e, 999999}; // never within deadline
  HealthCriterion crit{"dead", probe_after, &t};
  auto out = run_health_gate(CFG, make_hooks(e), &crit, 1);
  zassert_equal(out, GateOutcome::RevertRebooted);
  zassert_equal(e.confirm_calls, 0);
  zassert_equal(e.reboot_calls, 1);
  zassert_true(e.now >= CFG.deadline_ms);
}

ZTEST(health_gate, test_health_flapping_resets_dwell) {
  // Healthy at t=0, but flaps unhealthy before dwell elapses -> must not confirm early.
  static FakeEnv e;
  struct Flap {
    int64_t on1, off, on2;
  };
  static Flap fl{0, 1000, 5000};
  auto probe = +[](void *c) -> bool {
    (void)c;
    int64_t n = e.now;
    return (n >= fl.on1 && n < fl.off) || (n >= fl.on2);
  };
  HealthCriterion crit{"flap", probe, nullptr};
  auto out = run_health_gate(CFG, make_hooks(e), &crit, 1);
  zassert_equal(out, GateOutcome::Confirmed);
  // Confirm only after a continuous dwell starting no earlier than on2.
  zassert_true(e.now >= fl.on2 + CFG.dwell_ms);
}

ZTEST_SUITE(health_gate, NULL, NULL, NULL, NULL, NULL);
