/**
 * @file wav_dac.cpp
 * @brief WAV File DAC Emulator Driver
 *
 * Implements Zephyr DAC API by writing audio samples to a WAV file.
 * This driver is intended for testing and simulation on platforms
 * without real DAC hardware (e.g., native_sim).
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#define DT_DRV_COMPAT wav_dac

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(wav_dac, LOG_LEVEL_DBG);

/**
 * @brief WAV file header structure (44 bytes)
 */
struct wav_header {
  /* RIFF chunk descriptor */
  char riff[4];       /* "RIFF" */
  uint32_t file_size; /* File size - 8 */
  char wave[4];       /* "WAVE" */

  /* fmt sub-chunk */
  char fmt[4];              /* "fmt " */
  uint32_t fmt_size;        /* Size of fmt chunk (16 for PCM) */
  uint16_t audio_format;    /* Audio format (1 = PCM) */
  uint16_t num_channels;    /* Number of channels */
  uint32_t sample_rate;     /* Sample rate */
  uint32_t byte_rate;       /* Byte rate */
  uint16_t block_align;     /* Block align */
  uint16_t bits_per_sample; /* Bits per sample */

  /* data sub-chunk */
  char data[4];       /* "data" */
  uint32_t data_size; /* Size of data section */
} __packed;

/**
 * @brief WAV DAC device configuration (from devicetree and Kconfig)
 */
struct wav_dac_config {
  const char *output_file;
  uint8_t channels;
  uint8_t resolution;
};

/**
 * @brief WAV DAC runtime data
 */
struct wav_dac_data {
  FILE *file;
  uint32_t samples_written;
  bool is_open;
  struct k_mutex lock;
  bool channel_configured[8]; /* Max 8 channels */
};

/**
 * @brief Initialize WAV file with header
 */
static int wav_dac_init_file(const struct device *dev) {
  const struct wav_dac_config *cfg = static_cast<const struct wav_dac_config *>(dev->config);
  struct wav_dac_data *data = static_cast<struct wav_dac_data *>(dev->data);

  /* Open file for writing */
  data->file = fopen(cfg->output_file, "wb");
  if (!data->file) {
    LOG_ERR("Failed to open WAV file: %s", cfg->output_file);
    return -EIO;
  }

  /* Write WAV header (will be updated on close) */
  struct wav_header header = {
      .riff = {'R', 'I', 'F', 'F'},
      .file_size = 36, /* Will be updated */
      .wave = {'W', 'A', 'V', 'E'},
      .fmt = {'f', 'm', 't', ' '},
      .fmt_size = 16,
      .audio_format = 1, /* PCM */
      .num_channels = cfg->channels,
      .sample_rate = CONFIG_DAC_WAV_SAMPLE_RATE,
      .byte_rate = CONFIG_DAC_WAV_SAMPLE_RATE * cfg->channels * CONFIG_DAC_WAV_BITS_PER_SAMPLE / 8,
      .block_align = static_cast<uint16_t>(cfg->channels * CONFIG_DAC_WAV_BITS_PER_SAMPLE / 8),
      .bits_per_sample = CONFIG_DAC_WAV_BITS_PER_SAMPLE,
      .data = {'d', 'a', 't', 'a'},
      .data_size = 0 /* Will be updated */
  };

  size_t written = fwrite(&header, 1, sizeof(header), data->file);
  if (written != sizeof(header)) {
    LOG_ERR("Failed to write WAV header");
    fclose(data->file);
    data->file = NULL;
    return -EIO;
  }

  data->is_open = true;
  data->samples_written = 0;

  LOG_INF("WAV DAC initialized: %s (%u Hz, %u ch, %u bit)", cfg->output_file, CONFIG_DAC_WAV_SAMPLE_RATE, cfg->channels, CONFIG_DAC_WAV_BITS_PER_SAMPLE);

  return 0;
}

/**
 * @brief Update WAV header with final size
 */
static void wav_dac_update_header(const struct device *dev) {
  const struct wav_dac_config *cfg = static_cast<const struct wav_dac_config *>(dev->config);
  struct wav_dac_data *data = static_cast<struct wav_dac_data *>(dev->data);

  if (!data->file) {
    return;
  }

  uint32_t data_size = data->samples_written * cfg->channels * CONFIG_DAC_WAV_BITS_PER_SAMPLE / 8;
  uint32_t file_size = data_size + 36;

  /* Seek to file size field */
  fseek(data->file, 4, SEEK_SET);
  fwrite(&file_size, sizeof(file_size), 1, data->file);

  /* Seek to data size field */
  fseek(data->file, 40, SEEK_SET);
  fwrite(&data_size, sizeof(data_size), 1, data->file);

  LOG_INF("WAV file finalized: %u samples (%.2f seconds)", data->samples_written, (double)data->samples_written / CONFIG_DAC_WAV_SAMPLE_RATE);
}

/**
 * @brief DAC channel setup
 */
static int wav_dac_channel_setup(const struct device *dev, const struct dac_channel_cfg *channel_cfg) {
  const struct wav_dac_config *cfg = static_cast<const struct wav_dac_config *>(dev->config);
  struct wav_dac_data *data = static_cast<struct wav_dac_data *>(dev->data);

  if (channel_cfg->channel_id >= 8) {
    return -EINVAL;
  }

  /* Validate that DAC resolution is sufficient for target bits-per-sample */
  if (cfg->resolution < CONFIG_DAC_WAV_BITS_PER_SAMPLE) {
    LOG_ERR("DAC resolution (%u bits) is less than WAV bits-per-sample (%u bits)", 
            cfg->resolution, CONFIG_DAC_WAV_BITS_PER_SAMPLE);
    return -EINVAL;
  }

  k_mutex_lock(&data->lock, K_FOREVER);

  /* Initialize file on first channel setup */
  if (!data->is_open) {
    int ret = wav_dac_init_file(dev);
    if (ret != 0) {
      k_mutex_unlock(&data->lock);
      return ret;
    }
  }

  data->channel_configured[channel_cfg->channel_id] = true;

  k_mutex_unlock(&data->lock);

  LOG_DBG("Channel %u configured", channel_cfg->channel_id);
  return 0;
}

/**
 * @brief Write single value to DAC
 */
static int wav_dac_write_value(const struct device *dev, uint8_t channel, uint32_t value) {
  const struct wav_dac_config *cfg = static_cast<const struct wav_dac_config *>(dev->config);
  struct wav_dac_data *data = static_cast<struct wav_dac_data *>(dev->data);

  if (channel >= 8 || !data->channel_configured[channel]) {
    return -EINVAL;
  }

  k_mutex_lock(&data->lock, K_FOREVER);

  if (!data->is_open || !data->file) {
    k_mutex_unlock(&data->lock);
    return -EIO;
  }

  /* Scale value to sample bit depth */
  size_t written = 0;
  if (CONFIG_DAC_WAV_BITS_PER_SAMPLE == 16) {
    /* Scale from DAC resolution to 16-bit signed */
    uint16_t sample = static_cast<uint16_t>(value >> (cfg->resolution - 16));
    written = fwrite(&sample, sizeof(sample), 1, data->file);
  } else if (CONFIG_DAC_WAV_BITS_PER_SAMPLE == 8) {
    /* Scale from DAC resolution to 8-bit unsigned */
    uint8_t sample = static_cast<uint8_t>(value >> (cfg->resolution - 8));
    written = fwrite(&sample, sizeof(sample), 1, data->file);
  } else {
    LOG_ERR("Unsupported bits per sample: %d", CONFIG_DAC_WAV_BITS_PER_SAMPLE);
    k_mutex_unlock(&data->lock);
    return -ENOTSUP;
  }

  if (written != 1) {
    LOG_ERR("Failed to write sample to WAV file");
    k_mutex_unlock(&data->lock);
    return -EIO;
  }

  data->samples_written++;

  k_mutex_unlock(&data->lock);

  return 0;
}

/**
 * @brief Driver API structure
 */
static const struct dac_driver_api wav_dac_api = {
    .channel_setup = wav_dac_channel_setup,
    .write_value = wav_dac_write_value,
};

/**
 * @brief Device initialization
 */
static int wav_dac_init(const struct device *dev) __attribute__((used));
static int wav_dac_init(const struct device *dev) {
  struct wav_dac_data *data = static_cast<struct wav_dac_data *>(dev->data);

  k_mutex_init(&data->lock);
  data->is_open = false;
  data->samples_written = 0;
  memset(data->channel_configured, 0, sizeof(data->channel_configured));

  LOG_DBG("WAV DAC driver initialized");
  return 0;
}

/**
 * @brief Device shutdown (called at program exit on native_sim)
 */
static void wav_dac_shutdown(const struct device *dev) __attribute__((used));
static void wav_dac_shutdown(const struct device *dev) {
  struct wav_dac_data *data = static_cast<struct wav_dac_data *>(dev->data);

  k_mutex_lock(&data->lock, K_FOREVER);

  if (data->is_open && data->file) {
    wav_dac_update_header(dev);
    fclose(data->file);
    data->file = NULL;
    data->is_open = false;
  }

  k_mutex_unlock(&data->lock);
}

/* Devicetree macro magic */
#define WAV_DAC_INIT(inst)                                                                                                                                     \
  static struct wav_dac_data wav_dac_data_##inst;                                                                                                              \
                                                                                                                                                               \
  static const struct wav_dac_config wav_dac_config_##inst = {                                                                                                 \
      .output_file = DT_INST_PROP(inst, output_file),                                                                                                          \
      .channels = DT_INST_PROP(inst, channels),                                                                                                                \
      .resolution = DT_INST_PROP(inst, resolution),                                                                                                            \
  };                                                                                                                                                           \
                                                                                                                                                               \
  DEVICE_DT_INST_DEFINE(inst, wav_dac_init, wav_dac_shutdown, &wav_dac_data_##inst, &wav_dac_config_##inst, POST_KERNEL, CONFIG_DAC_WAV_INIT_PRIORITY,         \
                        &wav_dac_api);

DT_INST_FOREACH_STATUS_OKAY(WAV_DAC_INIT)
