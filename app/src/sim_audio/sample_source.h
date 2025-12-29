#ifndef SIM_AUDIO_SAMPLE_SOURCE_H_
#define SIM_AUDIO_SAMPLE_SOURCE_H_

#include <cstdint>

class SampleSource {
public:
  virtual ~SampleSource() = default;

  [[nodiscard]] virtual uint32_t sample_rate_hz() const noexcept = 0;
  virtual float next_sample_norm() = 0; // [-1.0 .. +1.0]
};

#endif // SIM_AUDIO_SAMPLE_SOURCE_H_
