#include "sample_clock.h"

#include <zephyr/kernel.h>

namespace {
constexpr uint64_t ns_per_s = 1000000000ULL;
}

SampleClock::SampleClock() {
  k_timer_init(&timer_, &SampleClock::timer_trampoline, nullptr);
  k_timer_user_data_set(&timer_, this);
}

void SampleClock::start(uint32_t rate_hz, tick_fn_t fn, void *user) {
  if (rate_hz == 0u || fn == nullptr)
    return;

  rate_hz_ = rate_hz;
  fn_ = fn;
  user_ = user;

  const uint64_t period_ns = ns_per_s / static_cast<uint64_t>(rate_hz_);
  k_timer_start(&timer_, K_NSEC(period_ns), K_NSEC(period_ns));
  running_ = true;
}

void SampleClock::stop() {
  k_timer_stop(&timer_);
  running_ = false;
}

void SampleClock::timer_trampoline(k_timer *t) {
  auto *self = static_cast<SampleClock *>(k_timer_user_data_get(t));
  if (!self || !self->fn_)
    return;
  self->fn_(self->user_);
}
