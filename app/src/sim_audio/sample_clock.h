#ifndef SIM_AUDIO_SAMPLE_CLOCK_H_
#define SIM_AUDIO_SAMPLE_CLOCK_H_

#include <cstdint>
#include <etl/delegate.h>
#include <zephyr/kernel.h>

class SampleClock {
public:
  SampleClock();

  void start(uint32_t rate_hz, etl::delegate<void()> fn);
  void stop();

  bool running() const { return running_; }
  uint32_t rate_hz() const { return rate_hz_; }

private:
  static void timer_trampoline(k_timer *t);

  k_timer timer_;
  bool running_{false};
  uint32_t rate_hz_{0};

  etl::delegate<void()> fn_{};
};

#endif // SIM_AUDIO_SAMPLE_CLOCK_H_
