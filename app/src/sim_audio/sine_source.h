#ifndef SIM_AUDIO_SINE_SOURCE_H_
#define SIM_AUDIO_SINE_SOURCE_H_

#include "sample_source.h"

#include <cstdint>

class SineSource final : public SampleSource {
public:
  void configure(uint32_t freq_hz, float amp_norm, uint32_t sample_rate_hz);

  [[nodiscard]] uint32_t sample_rate_hz() const noexcept override { return sample_rate_hz_; }
  float next_sample_norm() override;

  [[nodiscard]] uint32_t freq_hz() const noexcept { return freq_hz_; }
  [[nodiscard]] float amp_norm() const noexcept { return amp_norm_; }

private:
  uint32_t freq_hz_{};
  uint32_t sample_rate_hz_{};
  float amp_norm_{};
  float phase_rad_{};
};

#endif // SIM_AUDIO_SINE_SOURCE_H_
