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

#include <sa818/sa818_audio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sa818_audio, LOG_LEVEL_INF);

/**
 * @brief Initialize audio subsystem
 *
 * Sets up ADC for audio monitoring and prepares
 * for future DAC integration.
 */
sa818_result sa818_audio_init(const struct device *dev) {
  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(dev->config);

  /* Configure ADC channel for audio monitoring */
  int ret = adc_channel_setup_dt(&cfg->audio_in);
  if (ret != 0) {
    LOG_ERR("ADC channel setup failed: %d", ret);
    return SA818_ERROR_ADC;
  }

  LOG_INF("Audio subsystem initialized");
  return SA818_OK;
}

/**
 * @brief Set TX audio level (modulation)
 *
 * In future, this will control DAC output for audio modulation.
 * Currently placeholder.
 */
sa818_result sa818_audio_set_tx_level(const struct device *dev, uint8_t level) {
  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);

  k_mutex_lock(&data->lock, K_FOREVER);

  /* TODO: Implement DAC control when hardware supports it */
  LOG_DBG("TX audio level set to %d (not yet implemented)", level);

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
