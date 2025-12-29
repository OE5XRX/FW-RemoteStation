#include "wav_source.h"

#include "constants.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <span>
#include <unistd.h>

int WavSource::read_exact(int fd, void *dst, std::size_t n_bytes) {
  auto *p = static_cast<uint8_t *>(dst);
  std::size_t off = 0;
  while (off < n_bytes) {
    const ssize_t r = ::read(fd, p + off, n_bytes - off);
    if (r < 0)
      return -errno;
    if (r == 0)
      return sim_audio::err_io;
    off += static_cast<std::size_t>(r);
  }
  return 0;
}

int WavSource::parse_wav_into_buffer(int fd) {
  uint8_t riff_hdr[12]{};
  int rc = read_exact(fd, riff_hdr, sizeof(riff_hdr));
  if (rc)
    return rc;

  constexpr std::array<uint8_t, 4> riff_magic{'R', 'I', 'F', 'F'};
  constexpr std::array<uint8_t, 4> wave_magic{'W', 'A', 'V', 'E'};

  if (!std::equal(riff_magic.begin(), riff_magic.end(), riff_hdr) ||
      !std::equal(wave_magic.begin(), wave_magic.end(), riff_hdr + 8)) {
    return sim_audio::err_inval;
  }

  bool have_fmt = false;
  bool have_data = false;

  uint16_t audio_format = 0;
  uint16_t num_channels = 0;
  uint16_t bits_per_sample = 0;
  uint32_t sample_rate_hz = 0;

  uint32_t data_bytes = 0;
  off_t data_off = 0;

  for (;;) {
    uint8_t chunk_hdr[8]{};
    const ssize_t r = ::read(fd, chunk_hdr, sizeof(chunk_hdr));
    if (r == 0)
      break;
    if (r < 0)
      return -errno;
    if (r != static_cast<ssize_t>(sizeof(chunk_hdr)))
      return sim_audio::err_io;

    const uint32_t chunk_size = rd_u32_le(std::span<const uint8_t, 4>{chunk_hdr + 4, 4});

    constexpr std::array<uint8_t, 4> fmt_magic{'f', 'm', 't', ' '};
    constexpr std::array<uint8_t, 4> data_magic{'d', 'a', 't', 'a'};

    if (std::equal(fmt_magic.begin(), fmt_magic.end(), chunk_hdr)) {
      if (chunk_size < 16u || chunk_size > 32u)
        return sim_audio::err_inval;

      uint8_t fmt[32]{};
      rc = read_exact(fd, fmt, chunk_size);
      if (rc)
        return rc;

      audio_format = rd_u16_le(std::span<const uint8_t, 2>{fmt + 0, 2});
      num_channels = rd_u16_le(std::span<const uint8_t, 2>{fmt + 2, 2});
      sample_rate_hz = rd_u32_le(std::span<const uint8_t, 4>{fmt + 4, 4});
      bits_per_sample = rd_u16_le(std::span<const uint8_t, 2>{fmt + 14, 2});
      have_fmt = true;

    } else if (std::equal(data_magic.begin(), data_magic.end(), chunk_hdr)) {
      data_bytes = chunk_size;
      data_off = ::lseek(fd, 0, SEEK_CUR);
      if (data_off < 0)
        return -errno;

      if (::lseek(fd, static_cast<off_t>(chunk_size), SEEK_CUR) < 0)
        return -errno;
      have_data = true;

    } else {
      if (::lseek(fd, static_cast<off_t>(chunk_size), SEEK_CUR) < 0)
        return -errno;
    }

    if (have_fmt && have_data)
      break;
  }

  if (!have_fmt || !have_data)
    return sim_audio::err_inval;
  if (audio_format != 1u)
    return sim_audio::err_nosup; // PCM only
  if (num_channels != 1u)
    return sim_audio::err_nosup; // mono only
  if (bits_per_sample != 16u)
    return sim_audio::err_nosup; // s16 only
  if (sample_rate_hz == 0u)
    return sim_audio::err_inval;

  if (::lseek(fd, data_off, SEEK_SET) < 0)
    return -errno;

  const std::size_t available_samples = static_cast<std::size_t>(data_bytes / 2u);
  const std::size_t max_samples = buf_.size();
  const std::size_t samples_to_read = (available_samples > max_samples) ? max_samples : available_samples;

  for (std::size_t i = 0; i < samples_to_read; i++) {
    uint8_t b[2]{};
    rc = read_exact(fd, b, sizeof(b));
    if (rc)
      return rc;
    buf_[i] = static_cast<int16_t>(rd_u16_le(b));
  }

  count_samples_ = samples_to_read;
  idx_samples_ = 0;
  sample_rate_hz_ = sample_rate_hz;
  return 0;
}

int WavSource::load(const char *path) {
  const int fd = ::open(path, O_RDONLY);
  if (fd < 0)
    return -errno;

  const int rc = parse_wav_into_buffer(fd);
  ::close(fd);

  if (rc) {
    count_samples_ = 0;
    idx_samples_ = 0;
    sample_rate_hz_ = 0;
  }
  return rc;
}

float WavSource::next_sample_norm() {
  if (!loaded() || count_samples_ == 0u)
    return 0.0f;

  const int16_t s = buf_[idx_samples_++];
  if (idx_samples_ >= count_samples_)
    idx_samples_ = 0;

  // Convert to [-1, +1). Use 32768 to map -32768 -> -1.0 exactly.
  return static_cast<float>(s) / 32768.0f;
}
