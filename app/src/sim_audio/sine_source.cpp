#include "sine_source.h"
#include "constants.h"
#include <cmath>

void SineSource::configure(uint32_t freq_hz, float amp_norm,
                           uint32_t sample_rate_hz) {
  freq_hz_ = freq_hz;
  amp_norm_ = amp_norm;
  sample_rate_hz_ = sample_rate_hz;
  phase_rad_ = 0.0f;
}

float SineSource::next_sample_norm() {
  const float v = std::sin(phase_rad_) * amp_norm_; // [-amp..+amp]

  const float step_rad =
      sim_audio::two_pi *
      (static_cast<float>(freq_hz_) / static_cast<float>(sample_rate_hz_));
  phase_rad_ += step_rad;
  if (phase_rad_ >= sim_audio::two_pi) {
    phase_rad_ -= sim_audio::two_pi;
  }
  return v;
}
