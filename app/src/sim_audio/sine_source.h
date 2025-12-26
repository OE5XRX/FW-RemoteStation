#ifndef SIM_AUDIO_SINE_SOURCE_H_
#define SIM_AUDIO_SINE_SOURCE_H_

#include "sample_source.h"

#include <cstdint>

class SineSource final : public SampleSource {
public:
  void configure(uint32_t freq_hz, float amp_norm, uint32_t sample_rate_hz);

  uint32_t sample_rate_hz() const override { return sample_rate_hz_; }
  float next_sample_norm() override;

  uint32_t freq_hz() const { return freq_hz_; }
  float amp_norm() const { return amp_norm_; }

private:
  uint32_t freq_hz_{};
  uint32_t sample_rate_hz_{};
  float amp_norm_{};
  float phase_rad_{};
};

#endif // SIM_AUDIO_SINE_SOURCE_H_
