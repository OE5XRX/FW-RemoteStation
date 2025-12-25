#ifndef CONSTANTS_H_
#define CONSTANTS_H_

#include <cstdint>
#include <numbers>

namespace sim_audio {

// ADC
constexpr uint16_t adc_raw_min = 0u;
constexpr uint16_t adc_raw_max_12bit = 4095u;
constexpr uint16_t adc_raw_mid_12bit = adc_raw_max_12bit / 2u; // 2047

// Default generator
constexpr uint32_t default_sine_freq_hz = 1000u;
constexpr float default_sine_amp = 1.0f;
constexpr uint32_t default_gen_rate_hz = 8000u;

// WAV limits
constexpr uint32_t wav_max_rate_hz = 48000u;
constexpr uint32_t wav_max_duration_s = 20u;
constexpr uint32_t wav_max_samples = wav_max_rate_hz * wav_max_duration_s;

// Common “errors” (avoid bare -22 etc)
constexpr int err_inval = -22; // -EINVAL
constexpr int err_nosup = -95; // -ENOTSUP
constexpr int err_nodev = -19; // -ENODEV
constexpr int err_io = -5;     // -EIO

// Math
// constexpr float two_pi = 6.2831853071795864769f;
constexpr float two_pi = 2.0f * std::numbers::pi_v<float>;

} // namespace sim_audio

#endif // CONSTANTS_H_
