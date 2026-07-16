#include "health_gate.h"

#include <errno.h>
#include <zephyr/ztest.h>

using namespace boot;

// --- Fake environment -------------------------------------------------------
struct FakeEnv {
  int64_t now = 0;
  bool confirmed_flag = false; // starts unconfirmed (trial boot)
  int confirm_calls = 0;
  int reboot_calls = 0;
  // Confirm-failure scripting: the first `confirm_fail_count` confirm attempts
  // return -EIO; the rest return 0. -1 == fail forever (permanent failure).
  int confirm_fail_count = 0;
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
  auto *e = static_cast<FakeEnv *>(c);
  e->confirm_calls++;
  if (e->confirm_fail_count < 0) {
    return -EIO; // permanent failure
  }
  if (e->confirm_fail_count > 0) {
    e->confirm_fail_count--;
    return -EIO; // transient failure, will succeed later
  }
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

ZTEST(health_gate, test_confirm_transient_failure_retries) {
  // boot_write_img_confirmed() fails (e.g. flash busy) on the first two dwell
  // completions, then succeeds. The gate must NOT report Confirmed on the first
  // healthy dwell; it must reset the streak and keep retrying until the write
  // succeeds, all before the deadline.
  FakeEnv e;
  e.confirm_fail_count = 2; // first two confirm() calls return -EIO
  Thresh t{&e, 0};          // healthy immediately
  HealthCriterion crit{"x", probe_after, &t};
  auto out = run_health_gate(CFG, make_hooks(e), &crit, 1);
  zassert_equal(out, GateOutcome::Confirmed);
  zassert_equal(e.reboot_calls, 0);
  // Retried: succeeded only on the 3rd attempt.
  zassert_equal(e.confirm_calls, 3);
  zassert_true(e.confirm_calls > 1);
  // Each failed attempt restarts the dwell, so success is at least 3 dwells in
  // but still within the deadline.
  zassert_true(e.now >= 3 * CFG.dwell_ms);
  zassert_true(e.now < CFG.deadline_ms);
  zassert_equal(e.confirm_fail_count, 0); // scripted failures all consumed
}

ZTEST(health_gate, test_confirm_permanent_failure_reverts) {
  // boot_write_img_confirmed() never succeeds. The deadline backstop must fire:
  // the gate reverts (reboots) rather than looping forever or falsely
  // reporting Confirmed.
  FakeEnv e;
  e.confirm_fail_count = -1; // fail forever
  Thresh t{&e, 0};           // healthy immediately
  HealthCriterion crit{"x", probe_after, &t};
  auto out = run_health_gate(CFG, make_hooks(e), &crit, 1);
  zassert_equal(out, GateOutcome::RevertRebooted);
  zassert_equal(e.reboot_calls, 1);
  zassert_true(e.confirm_calls > 1); // tried repeatedly before giving up
  zassert_true(e.now >= CFG.deadline_ms);
}

ZTEST_SUITE(health_gate, NULL, NULL, NULL, NULL, NULL);
