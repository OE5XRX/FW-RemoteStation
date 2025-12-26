#ifndef SIM_AUDIO_SAMPLE_CLOCK_H_
#define SIM_AUDIO_SAMPLE_CLOCK_H_

#include <cstdint>
#include <zephyr/kernel.h>

class SampleClock {
public:
  using tick_fn_t = void (*)(void *user);

  SampleClock();

  void start(uint32_t rate_hz, tick_fn_t fn, void *user);
  void stop();

  bool running() const { return running_; }
  uint32_t rate_hz() const { return rate_hz_; }

private:
  static void timer_trampoline(k_timer *t);

  k_timer timer_;
  bool running_{false};
  uint32_t rate_hz_{0};

  tick_fn_t fn_{nullptr};
  void *user_{nullptr};
};

#endif // SIM_AUDIO_SAMPLE_CLOCK_H_
