/**
 * @file sa818_audio.cpp
 * @brief SA818 Audio Subsystem Implementation
 *
 * Implements ADC-based audio monitoring and DAC control for
 * SA818 radio module audio paths.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#include "sa818_priv.h"

#include <cmath>
#include <sa818/sa818_audio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sa818_audio, LOG_LEVEL_INF);

/* Test tone constants */
#define TEST_TONE_SAMPLE_RATE_HZ 8000
#define TEST_TONE_UPDATE_INTERVAL_US (1000000 / TEST_TONE_SAMPLE_RATE_HZ) /* 125 us */
#define TEST_TONE_MIN_FREQ_HZ 100
#define TEST_TONE_MAX_FREQ_HZ 3000
#define TEST_TONE_MAX_DURATION_MS 3600000 /* 1 hour */

/* Forward declarations */
static void test_tone_work_handler(struct k_work *work);

/**
 * @brief Initialize audio subsystem
 *
 * Sets up ADC for audio monitoring and DAC for audio output.
 */
sa818_result sa818_audio_init(const struct device *dev) {
  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(dev->config);
  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);

  /* Initialize test tone work queue (handle possible re-init safely) */
  if (data->test_tone_dev != nullptr) {
    /* Cancel any pending or scheduled test tone work before reinitializing */
    k_work_cancel_delayable(&data->test_tone_work);
  }
  k_work_init_delayable(&data->test_tone_work, test_tone_work_handler);
  data->test_tone_dev = dev;
  data->test_tone_active = false;

  /* Configure ADC channel for audio monitoring */
  int ret = adc_channel_setup_dt(&cfg->audio_in);
  if (ret != 0) {
    LOG_ERR("ADC channel setup failed: %d", ret);
    return SA818_ERROR_ADC;
  }

  /* Configure DAC channel for audio output */
  struct dac_channel_cfg dac_cfg = {
      .channel_id = cfg->audio_out_channel,
      .resolution = cfg->audio_out_resolution,
  };

  ret = dac_channel_setup(cfg->audio_out_dev, &dac_cfg);
  if (ret != 0) {
    LOG_ERR("DAC channel setup failed: %d", ret);
    return SA818_ERROR_DAC;
  }
  LOG_INF("DAC channel %u configured (%u-bit)", cfg->audio_out_channel, cfg->audio_out_resolution);

  LOG_INF("Audio subsystem initialized");
  return SA818_OK;
}

/**
 * @brief Set TX audio level (modulation)
 *
 * Controls DAC output for audio modulation. The level is scaled
 * to the DAC resolution and written as a single sample.
 */
sa818_result sa818_audio_set_tx_level(const struct device *dev, uint8_t level) {
  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(dev->config);
  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);

  k_mutex_lock(&data->lock, K_FOREVER);

  if (!data->audio_tx_enabled) {
    LOG_DBG("TX audio disabled, ignoring level set");
    k_mutex_unlock(&data->lock);
    return SA818_OK;
  }

  /* Scale 8-bit level (0-255) to DAC resolution */
  uint32_t dac_value = (static_cast<uint32_t>(level) << (cfg->audio_out_resolution - 8));

  int ret = dac_write_value(cfg->audio_out_dev, cfg->audio_out_channel, dac_value);
  if (ret != 0) {
    LOG_ERR("DAC write failed: %d", ret);
    k_mutex_unlock(&data->lock);
    return SA818_ERROR_DAC;
  }

  LOG_DBG("TX audio level set to %d (DAC: 0x%04x)", level, dac_value);

  k_mutex_unlock(&data->lock);
  return SA818_OK;
}

/**
 * @brief Get RX audio level (demodulation)
 *
 * Reads ADC to get current audio input level.
 * Can be used for monitoring squelch or signal strength.
 */
sa818_result sa818_audio_get_rx_level(const struct device *dev, uint16_t *level) {
  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(dev->config);
  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);

  if (!level) {
    return SA818_ERROR_INVALID_PARAM;
  }

  k_mutex_lock(&data->lock, K_FOREVER);

  /* Read ADC value */
  uint16_t buf;
  struct adc_sequence sequence = {
      .buffer = &buf,
      .buffer_size = sizeof(buf),
  };

  int ret = adc_sequence_init_dt(&cfg->audio_in, &sequence);
  if (ret != 0) {
    LOG_ERR("ADC sequence init failed: %d", ret);
    k_mutex_unlock(&data->lock);
    return SA818_ERROR_ADC;
  }

  ret = adc_read_dt(&cfg->audio_in, &sequence);
  if (ret != 0) {
    LOG_ERR("ADC read failed: %d", ret);
    k_mutex_unlock(&data->lock);
    return SA818_ERROR_ADC;
  }

  *level = buf;
  LOG_DBG("RX audio level: %d", *level);

  k_mutex_unlock(&data->lock);
  return SA818_OK;
}

/**
 * @brief Enable/disable audio paths
 *
 * Controls whether RX and TX audio paths are active.
 * This can be used to mute audio or save power.
 */
sa818_result sa818_audio_enable_path(const struct device *dev, bool rx_enable, bool tx_enable) {
  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);

  k_mutex_lock(&data->lock, K_FOREVER);

  data->audio_rx_enabled = rx_enable;
  data->audio_tx_enabled = tx_enable;

  LOG_INF("Audio paths: RX=%s TX=%s", rx_enable ? "enabled" : "disabled", tx_enable ? "enabled" : "disabled");

  k_mutex_unlock(&data->lock);
  return SA818_OK;
}

/**
 * @brief Work queue handler for test tone generation
 *
 * Called periodically to generate sine wave samples and output to DAC.
 */
static void test_tone_work_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct sa818_data *data = CONTAINER_OF(dwork, struct sa818_data, test_tone_work);

  const struct device *dev = data->test_tone_dev;
  if (!dev) {
    LOG_ERR("Device pointer not set in test_tone_work_handler");
    return;
  }

  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(dev->config);

  k_mutex_lock(&data->lock, K_FOREVER);

  /* Check if test tone is still active */
  if (!data->test_tone_active) {
    k_mutex_unlock(&data->lock);
    return;
  }

  /* Check if duration has expired */
  if (data->test_tone_end_time > 0) {
    int64_t now = k_uptime_get();
    if (now >= data->test_tone_end_time) {
      LOG_INF("Test tone duration expired");
      data->test_tone_active = false;
      data->audio_tx_enabled = false;

      /* Reset DAC to midpoint before stopping */
      uint32_t dac_max = (1U << cfg->audio_out_resolution) - 1;
      uint32_t dac_midpoint = dac_max / 2;
      dac_write_value(cfg->audio_out_dev, cfg->audio_out_channel, dac_midpoint);

      k_mutex_unlock(&data->lock);
      return;
    }
  }

  /* Generate sine wave sample */
  float sample = std::sin(data->test_tone_phase) * (static_cast<float>(data->test_tone_amplitude) / 255.0f);

  /* Scale to DAC range (0 to max, centered around midpoint) */
  uint32_t dac_max = (1U << cfg->audio_out_resolution) - 1;
  uint32_t dac_midpoint = dac_max / 2;
  int32_t dac_value = dac_midpoint + static_cast<int32_t>(sample * static_cast<float>(dac_midpoint));

  /* Clamp to valid range */
  if (dac_value < 0) {
    dac_value = 0;
  }
  if (dac_value > static_cast<int32_t>(dac_max)) {
    dac_value = dac_max;
  }

  /* Write to DAC */
  int ret = dac_write_value(cfg->audio_out_dev, cfg->audio_out_channel, static_cast<uint32_t>(dac_value));
  if (ret != 0) {
    LOG_ERR("DAC write failed during test tone: %d", ret);
    data->test_tone_active = false;
    data->audio_tx_enabled = false;
    k_mutex_unlock(&data->lock);
    return;
  }

  /* Update phase for next sample */
  constexpr double two_pi = 2.0 * M_PI;
  double phase_increment = two_pi * static_cast<double>(data->test_tone_freq) / static_cast<double>(TEST_TONE_SAMPLE_RATE_HZ);
  data->test_tone_phase += static_cast<float>(phase_increment);

  /* Wrap phase to [0, 2*PI) using modulo to prevent accumulation errors */
  data->test_tone_phase = std::fmod(data->test_tone_phase, static_cast<float>(two_pi));
  if (data->test_tone_phase < 0.0f) {
    data->test_tone_phase += static_cast<float>(two_pi);
  }

  k_mutex_unlock(&data->lock);

  /* Schedule next update */
  k_work_schedule(&data->test_tone_work, K_USEC(TEST_TONE_UPDATE_INTERVAL_US));
}

/**
 * @brief Generate test tone on TX audio output
 *
 * Generates a sine wave test tone at the specified frequency and amplitude.
 * The tone can be continuous (duration_ms=0) or timed.
 */
sa818_result sa818_audio_generate_test_tone(const struct device *dev, uint16_t freq_hz, uint32_t duration_ms, uint8_t amplitude) {
  if (!dev || !device_is_ready(dev)) {
    return SA818_ERROR_INVALID_DEVICE;
  }

  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);
  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(dev->config);

  /* Validate parameters */
  if (freq_hz < TEST_TONE_MIN_FREQ_HZ || freq_hz > TEST_TONE_MAX_FREQ_HZ) {
    LOG_ERR("Invalid frequency: %u Hz (valid range: %u-%u Hz)", freq_hz, TEST_TONE_MIN_FREQ_HZ, TEST_TONE_MAX_FREQ_HZ);
    return SA818_ERROR_INVALID_PARAM;
  }

  if (duration_ms > TEST_TONE_MAX_DURATION_MS) {
    LOG_ERR("Invalid duration: %u ms (maximum: %u ms)", duration_ms, TEST_TONE_MAX_DURATION_MS);
    return SA818_ERROR_INVALID_PARAM;
  }

  if (!cfg->audio_out_dev || !device_is_ready(cfg->audio_out_dev)) {
    LOG_ERR("DAC device not available");
    return SA818_ERROR_DAC;
  }

  k_mutex_lock(&data->lock, K_FOREVER);

  /* Stop any existing test tone */
  if (data->test_tone_active) {
    LOG_WRN("Stopping existing test tone");
    data->test_tone_active = false;
    k_work_cancel_delayable(&data->test_tone_work);
  }

  /* Initialize test tone state */
  data->test_tone_freq = freq_hz;
  data->test_tone_amplitude = amplitude;
  data->test_tone_phase = 0.0f;
  data->test_tone_active = true;

  /* Calculate end time if duration is specified */
  if (duration_ms > 0) {
    data->test_tone_end_time = k_uptime_get() + duration_ms;
    LOG_INF("Starting test tone: %u Hz, %u ms, amplitude %u", freq_hz, duration_ms, amplitude);
  } else {
    data->test_tone_end_time = 0; /* Continuous */
    LOG_INF("Starting continuous test tone: %u Hz, amplitude %u", freq_hz, amplitude);
  }

  /* Enable TX audio path */
  data->audio_tx_enabled = true;

  k_mutex_unlock(&data->lock);

  /* Start work queue
   *
   * Note:
   * - Using K_NO_WAIT schedules the first test tone sample to run as soon
   *   as the system work queue can execute it (minimal initial delay).
   * - Subsequent samples are timed inside test_tone_work_handler based on
   *   TEST_TONE_SAMPLE_RATE_HZ / TEST_TONE_UPDATE_INTERVAL_US.
   * - If the system work queue is heavily loaded, the actual timing of the
   *   first (and possibly later) samples may deviate from the ideal schedule.
   */
  k_work_schedule(&data->test_tone_work, K_NO_WAIT);

  return SA818_OK;
}

/**
 * @brief Stop test tone generation
 *
 * Stops any active test tone and disables the TX audio path.
 */
sa818_result sa818_audio_stop_test_tone(const struct device *dev) {
  if (!dev || !device_is_ready(dev)) {
    return SA818_ERROR_INVALID_DEVICE;
  }

  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);

  k_mutex_lock(&data->lock, K_FOREVER);

  if (!data->test_tone_active) {
    LOG_DBG("No test tone active");
    k_mutex_unlock(&data->lock);
    return SA818_OK;
  }

  /* Stop test tone */
  data->test_tone_active = false;
  k_work_cancel_delayable(&data->test_tone_work);

  /* Reset DAC to midpoint before disabling TX path */
  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(dev->config);
  uint32_t dac_max = (1U << cfg->audio_out_resolution) - 1;
  uint32_t dac_midpoint = dac_max / 2;
  dac_write_value(cfg->audio_out_dev, cfg->audio_out_channel, dac_midpoint);

  /* Disable TX audio path */
  data->audio_tx_enabled = false;

  LOG_INF("Test tone stopped");

  k_mutex_unlock(&data->lock);

  return SA818_OK;
}
