#ifndef SIM_AUDIO_WAV_SOURCE_H_
#define SIM_AUDIO_WAV_SOURCE_H_

#include "constants.h"
#include "sample_source.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <zephyr/shell/shell.h>

class WavSource final : public SampleSource {
public:
  [[nodiscard]] int load(const char *path);

  [[nodiscard]] bool loaded() const noexcept { return (count_samples_ > 0u) && (sample_rate_hz_ > 0u); }
  [[nodiscard]] uint32_t sample_rate_hz() const noexcept override { return sample_rate_hz_; }
  float next_sample_norm() override; // loops

  [[nodiscard]] std::size_t pos_samples() const noexcept { return idx_samples_; }
  [[nodiscard]] std::size_t count_samples() const noexcept { return count_samples_; }

private:
  static int read_exact(int fd, void *dst, std::size_t n_bytes);
  static uint16_t rd_u16_le(const uint8_t *p);
  static uint32_t rd_u32_le(const uint8_t *p);
  int parse_wav_into_buffer(int fd);

private:
  std::array<int16_t, sim_audio::wav_max_samples> buf_{};
  std::size_t count_samples_{0};
  std::size_t idx_samples_{0};
  uint32_t sample_rate_hz_{0};
};

#endif // SIM_AUDIO_WAV_SOURCE_H_
