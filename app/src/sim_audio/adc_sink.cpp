#include "adc_sink.h"

#include <zephyr/device.h>
#include <zephyr/drivers/adc/adc_emul.h>

AdcSink::AdcSink(const device *adc_dev, uint8_t channel_id)
    : adc_dev_(adc_dev), channel_id_(channel_id) {}

bool AdcSink::ready() const { return adc_dev_ && device_is_ready(adc_dev_); }

void AdcSink::write_raw_12(uint16_t raw_12) {
  if (!ready())
    return;

  if (raw_12 > sim_audio::adc_raw_max_12bit) {
    raw_12 = sim_audio::adc_raw_max_12bit;
  }
  (void)adc_emul_const_raw_value_set(adc_dev_, channel_id_, raw_12);
}

void AdcSink::write_norm(float sample_norm) {
  if (sample_norm < -1.0f)
    sample_norm = -1.0f;
  if (sample_norm > 1.0f)
    sample_norm = 1.0f;

  // [-1,+1] -> [0,1]
  const float mapped_01 = (sample_norm + 1.0f) * 0.5f;
  const float raw_f =
      mapped_01 * static_cast<float>(sim_audio::adc_raw_max_12bit);

  int32_t raw_i = static_cast<int32_t>(raw_f + 0.5f); // rounding
  if (raw_i < sim_audio::adc_raw_min)
    raw_i = sim_audio::adc_raw_min;
  if (raw_i > sim_audio::adc_raw_max_12bit)
    raw_i = sim_audio::adc_raw_max_12bit;

  write_raw_12(static_cast<uint16_t>(raw_i));
}
