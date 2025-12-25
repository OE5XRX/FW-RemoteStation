#ifndef SIM_AUDIO_ADC_SINK_H_
#define SIM_AUDIO_ADC_SINK_H_

#include "constants.h"
#include <cstdint>

struct device;

class AdcSink {
public:
  AdcSink(const device *adc_dev, uint8_t channel_id);

  bool ready() const;

  // [-1,+1] -> 0..4095 written into adc emul
  void write_norm(float sample_norm);

  // direct raw write (0..4095)
  void write_raw_12(uint16_t raw_12);

private:
  const device *adc_dev_;
  uint8_t channel_id_;
};

#endif // SIM_AUDIO_ADC_SINK_H_
