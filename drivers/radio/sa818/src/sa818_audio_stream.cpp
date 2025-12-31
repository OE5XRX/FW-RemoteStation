/**
 * @file sa818_audio_stream.cpp
 * @brief SA818 Audio Streaming Implementation
 *
 * Implements generic audio streaming interface using callbacks.
 * Hardware-agnostic - can be connected to USB, I2S, files, network, etc.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#include "sa818_priv.h"

#include <sa818/sa818_audio_stream.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sa818_audio_stream, LOG_LEVEL_INF);

/* Audio streaming configuration */
#define SA818_AUDIO_BUFFER_SIZE 32 /* Samples per callback */
#define SA818_AUDIO_SAMPLE_SIZE 2  /* 16-bit = 2 bytes */

/**
 * @brief Audio streaming context
 */
struct sa818_audio_stream_ctx {
  const struct device *dev;
  struct sa818_audio_callbacks callbacks;
  struct sa818_audio_format format;

  struct k_work_delayable audio_work;
  struct k_mutex lock;
  bool streaming;

  /* Buffers for audio processing */
  uint8_t tx_buffer[SA818_AUDIO_BUFFER_SIZE * SA818_AUDIO_SAMPLE_SIZE];
  uint8_t rx_buffer[SA818_AUDIO_BUFFER_SIZE * SA818_AUDIO_SAMPLE_SIZE];
};

static struct sa818_audio_stream_ctx audio_ctx;

/**
 * @brief Audio processing work handler
 *
 * Periodically processes audio:
 * - Calls tx_request callback to get audio for TX
 * - Writes samples to DAC
 * - Reads samples from ADC
 * - Calls rx_data callback with received audio
 */
static void audio_stream_work_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct sa818_audio_stream_ctx *ctx = CONTAINER_OF(dwork, struct sa818_audio_stream_ctx, audio_work);

  k_mutex_lock(&ctx->lock, K_FOREVER);
  bool streaming = ctx->streaming;
  k_mutex_unlock(&ctx->lock);

  if (!streaming) {
    return;
  }

  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(ctx->dev->config);
  struct sa818_data *data = static_cast<struct sa818_data *>(ctx->dev->data);

  /* Process TX: Get audio from application -> DAC */
  if (ctx->callbacks.tx_request && data->audio_tx_enabled) {
    size_t bytes = ctx->callbacks.tx_request(ctx->dev, ctx->tx_buffer, sizeof(ctx->tx_buffer), ctx->callbacks.user_data);

    if (bytes >= SA818_AUDIO_SAMPLE_SIZE) {
      /* Write first sample to DAC */
      int16_t pcm_sample = (int16_t)((ctx->tx_buffer[1] << 8) | ctx->tx_buffer[0]);
      /* Scale signed 16-bit (-32768 to 32767) to unsigned DAC range */
      uint32_t dac_value = ((uint32_t)(pcm_sample + 32768) << (cfg->audio_out_resolution - 16));
      dac_write_value(cfg->audio_out_dev, cfg->audio_out_channel, dac_value);
    }
  }

  /* Process RX: ADC -> Application callback */
  if (ctx->callbacks.rx_data && data->audio_rx_enabled) {
    /* Read ADC */
    uint16_t adc_sequence_buf[1];
    struct adc_sequence sequence = {
        .buffer = adc_sequence_buf,
        .buffer_size = sizeof(adc_sequence_buf),
    };

    int ret = adc_read_dt(&cfg->audio_in, &sequence);
    if (ret == 0) {
      /* Convert ADC value to 16-bit PCM */
      int16_t adc_value = (int16_t)adc_sequence_buf[0];
      /* Scale 12-bit ADC (0-4095) to signed 16-bit (-32768 to 32767) */
      int16_t pcm_sample = (int16_t)((adc_value - 2048) << 4);

      ctx->rx_buffer[0] = (uint8_t)(pcm_sample & 0xFF);
      ctx->rx_buffer[1] = (uint8_t)((pcm_sample >> 8) & 0xFF);

      /* Call application callback */
      ctx->callbacks.rx_data(ctx->dev, ctx->rx_buffer, SA818_AUDIO_SAMPLE_SIZE, ctx->callbacks.user_data);
    }
  }

  /* Reschedule based on sample rate */
  uint32_t period_us = 1000000 / ctx->format.sample_rate;
  k_work_reschedule(&ctx->audio_work, K_USEC(period_us));
}

/**
 * @brief Register audio streaming callbacks
 */
sa818_result sa818_audio_stream_register(const struct device *dev, const struct sa818_audio_callbacks *callbacks) {
  if (!dev || !callbacks) {
    return SA818_ERROR_INVALID_PARAM;
  }

  audio_ctx.dev = dev;
  audio_ctx.callbacks = *callbacks;

  LOG_INF("Audio callbacks registered");
  return SA818_OK;
}

/**
 * @brief Start audio streaming
 */
sa818_result sa818_audio_stream_start(const struct device *dev, const struct sa818_audio_format *format) {
  if (!dev || !format) {
    return SA818_ERROR_INVALID_PARAM;
  }

  if (audio_ctx.streaming) {
    LOG_WRN("Audio streaming already active");
    return SA818_OK;
  }

  /* Store format */
  audio_ctx.format = *format;
  audio_ctx.dev = dev;

  /* Initialize mutex if not already done */
  static bool mutex_initialized = false;
  if (!mutex_initialized) {
    k_mutex_init(&audio_ctx.lock);
    mutex_initialized = true;
  }

  /* Initialize work queue if not already done */
  k_work_init_delayable(&audio_ctx.audio_work, audio_stream_work_handler);

  /* Start streaming */
  k_mutex_lock(&audio_ctx.lock, K_FOREVER);
  audio_ctx.streaming = true;
  k_mutex_unlock(&audio_ctx.lock);
  
  k_work_reschedule(&audio_ctx.audio_work, K_MSEC(1));

  LOG_INF("Audio streaming started: %u Hz, %u-bit, %u ch", format->sample_rate, format->bit_depth, format->channels);

  return SA818_OK;
}

/**
 * @brief Stop audio streaming
 */
sa818_result sa818_audio_stream_stop(const struct device *dev) {
  if (!dev) {
    return SA818_ERROR_INVALID_PARAM;
  }

  k_mutex_lock(&audio_ctx.lock, K_FOREVER);
  audio_ctx.streaming = false;
  k_mutex_unlock(&audio_ctx.lock);
  
  k_work_cancel_delayable(&audio_ctx.audio_work);

  LOG_INF("Audio streaming stopped");
  return SA818_OK;
}

/**
 * @brief Get current audio format
 */
sa818_result sa818_audio_stream_get_format(const struct device *dev, struct sa818_audio_format *format) {
  if (!dev || !format) {
    return SA818_ERROR_INVALID_PARAM;
  }

  *format = audio_ctx.format;
  return SA818_OK;
}
