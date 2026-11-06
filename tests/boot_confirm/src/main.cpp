#include "boot_confirm_timing.h"
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
  // Fake task_wdt: feeds counted, longest gap between consecutive feeds (and
  // from gate start to the first feed) tracked in simulated time.
  int feed_calls = 0;
  int64_t last_feed = 0;
  int64_t max_feed_gap = 0;
  // Simulated time at which reboot() was called; -1 == never.
  int64_t reboot_at = -1;
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
  auto *e = static_cast<FakeEnv *>(c);
  e->reboot_calls++;
  e->reboot_at = e->now;
}
static void f_feed(void *c) {
  auto *e = static_cast<FakeEnv *>(c);
  const int64_t gap = e->now - e->last_feed;
  if (gap > e->max_feed_gap) {
    e->max_feed_gap = gap;
  }
  e->last_feed = e->now;
  e->feed_calls++;
}

static GateHooks make_hooks(FakeEnv &e) {
  return GateHooks{f_now, f_sleep, f_confd, f_confirm, f_reboot, f_feed, &e};
}
// Same numbers the fm_board gate thread runs with.
static const GateConfig CFG{boot::timing::GATE_DEADLINE_MS, boot::timing::GATE_DWELL_MS, boot::timing::GATE_POLL_MS};

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

// Probe that blocks (advances simulated time) for `block_ms` on every call and
// reports healthy only once `healthy_at` is reached -- models probe_sa818
// waiting out the SA818 AT timeout when the radio does not answer.
struct Blocking {
  FakeEnv *e;
  int64_t block_ms;
  int64_t healthy_at;
};
static bool probe_blocking(void *c) {
  auto *b = static_cast<Blocking *>(c);
  b->e->now += b->block_ms;
  return b->e->now >= b->healthy_at;
}

// The task_wdt period the gate ran with before the fix (GATE_POLL_MS * 4).
static constexpr int64_t OLD_WDT_PERIOD_MS = 4 * boot::timing::GATE_POLL_MS;

ZTEST(health_gate, test_silent_sa818_reverts_at_deadline_not_watchdog) {
  // Regression (HIL 2026-11-06): no 12 V -> every AT+DMOCONNECT runs into the
  // full AT timeout. The gate must keep polling until the deadline and revert
  // via reboot(); the watchdog must never see a feed gap >= its period.
  FakeEnv e;
  Blocking b{&e, SA818_AT_TIMEOUT_MS, INT64_MAX};
  HealthCriterion crit{"sa818", probe_blocking, &b};
  auto out = run_health_gate(CFG, make_hooks(e), &crit, 1);
  zassert_equal(out, GateOutcome::RevertRebooted);
  zassert_equal(e.reboot_calls, 1);
  zassert_equal(e.confirm_calls, 0);
  // The scenario really exercises the bug: the old 1000 ms period is exceeded...
  zassert_true(e.max_feed_gap >= OLD_WDT_PERIOD_MS, "gap %lld", (long long)e.max_feed_gap);
  // ...but the new period is not, so task_wdt stays quiet until the deadline.
  zassert_true(e.max_feed_gap < boot::timing::WDT_PERIOD_MS, "gap %lld >= wdt %u", (long long)e.max_feed_gap, boot::timing::WDT_PERIOD_MS);
  zassert_true(e.max_feed_gap <= boot::timing::MAX_FEED_GAP_MS);
  // Revert happens at the deadline -- not ~2.5 s in -- and at most one slow
  // sweep + poll late.
  zassert_true(e.reboot_at >= CFG.deadline_ms);
  zassert_true(e.reboot_at <= CFG.deadline_ms + boot::timing::MAX_FEED_GAP_MS);
  zassert_true(e.feed_calls > 1);
}

ZTEST(health_gate, test_worst_case_sweep_stays_inside_wdt_period) {
  // Worst case: the probe first waits behind another thread's in-flight AT
  // command on the driver mutex, then runs into its own AT timeout.
  FakeEnv e;
  Blocking b{&e, boot::timing::PROBE_SWEEP_WORST_MS, INT64_MAX};
  HealthCriterion crit{"sa818", probe_blocking, &b};
  auto out = run_health_gate(CFG, make_hooks(e), &crit, 1);
  zassert_equal(out, GateOutcome::RevertRebooted);
  zassert_equal(e.reboot_calls, 1);
  zassert_equal(e.max_feed_gap, boot::timing::MAX_FEED_GAP_MS);
  zassert_true(e.max_feed_gap < boot::timing::WDT_PERIOD_MS);
}

ZTEST(health_gate, test_slow_but_answering_sa818_confirms) {
  // With 12 V the radio answers, but each round-trip is slow (~1 s) and it only
  // comes up after 10 s. Slow probes must neither trip the watchdog nor stop
  // the gate from confirming before the deadline.
  FakeEnv e;
  Blocking b{&e, 1000, 10000};
  HealthCriterion crit{"sa818", probe_blocking, &b};
  auto out = run_health_gate(CFG, make_hooks(e), &crit, 1);
  zassert_equal(out, GateOutcome::Confirmed);
  zassert_equal(e.reboot_calls, 0);
  zassert_true(e.max_feed_gap < boot::timing::WDT_PERIOD_MS);
  zassert_true(e.now < CFG.deadline_ms);
}

ZTEST(health_gate, test_dwell_met_on_slow_sweep_crossing_deadline_confirms) {
  // Radio healthy from 26 s on, each round-trip slow (2 s): the sweep that
  // completes the dwell also ends past the deadline. Confirm is evaluated
  // before the deadline check, so a healthy image is kept, not reverted.
  FakeEnv e;
  Blocking b{&e, SA818_AT_TIMEOUT_MS, 26000};
  HealthCriterion crit{"sa818", probe_blocking, &b};
  auto out = run_health_gate(CFG, make_hooks(e), &crit, 1);
  zassert_equal(out, GateOutcome::Confirmed);
  zassert_equal(e.reboot_calls, 0);
  zassert_true(e.now >= CFG.deadline_ms, "now %lld", (long long)e.now);
  zassert_true(e.max_feed_gap < boot::timing::WDT_PERIOD_MS);
}

ZTEST(health_gate, test_confirm_failing_during_slow_sweeps_reverts_at_deadline) {
  // Healthy but slow radio and a confirm write that never succeeds: the gate
  // keeps retrying without tripping the watchdog and reverts at the deadline.
  FakeEnv e;
  e.confirm_fail_count = -1;
  Blocking b{&e, SA818_AT_TIMEOUT_MS, 0};
  HealthCriterion crit{"sa818", probe_blocking, &b};
  auto out = run_health_gate(CFG, make_hooks(e), &crit, 1);
  zassert_equal(out, GateOutcome::RevertRebooted);
  zassert_equal(e.reboot_calls, 1);
  zassert_true(e.confirm_calls > 1);
  zassert_true(e.reboot_at >= CFG.deadline_ms);
  zassert_true(e.max_feed_gap < boot::timing::WDT_PERIOD_MS);
}

ZTEST(health_gate, test_blocking_probe_skipped_when_earlier_criterion_fails) {
  // Mirrors the fm_board order (usb, shell, sa818): while USB is unconfigured
  // the blocking SA818 probe is never reached, so sweeps stay fast.
  FakeEnv e;
  Thresh usb{&e, 999999};
  Blocking sa{&e, SA818_AT_TIMEOUT_MS, 0};
  HealthCriterion crit[] = {{"usb", probe_after, &usb}, {"sa818", probe_blocking, &sa}};
  auto out = run_health_gate(CFG, make_hooks(e), crit, 2);
  zassert_equal(out, GateOutcome::RevertRebooted);
  zassert_equal(e.max_feed_gap, CFG.poll_interval_ms);
  zassert_equal(e.reboot_at, CFG.deadline_ms);
}

ZTEST(health_gate, test_feed_called_every_sweep) {
  // Fast probes: one feed per loop iteration, gap == poll interval.
  FakeEnv e;
  Thresh t{&e, 999999};
  HealthCriterion crit{"dead", probe_after, &t};
  (void)run_health_gate(CFG, make_hooks(e), &crit, 1);
  zassert_equal(e.feed_calls, CFG.deadline_ms / CFG.poll_interval_ms + 1);
  zassert_equal(e.max_feed_gap, CFG.poll_interval_ms);
}

ZTEST(health_gate, test_null_feed_hook_is_allowed) {
  FakeEnv e;
  Thresh t{&e, 0};
  HealthCriterion crit{"x", probe_after, &t};
  GateHooks h = make_hooks(e);
  h.feed = nullptr;
  zassert_equal(run_health_gate(CFG, h, &crit, 1), GateOutcome::Confirmed);
}

ZTEST_SUITE(health_gate, NULL, NULL, NULL, NULL, NULL);
