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
  bool streaming;

  /* Buffers for audio processing */
  uint8_t tx_buffer[SA818_AUDIO_BUFFER_SIZE * SA818_AUDIO_SAMPLE_SIZE];
  uint8_t rx_buffer[SA818_AUDIO_BUFFER_SIZE * SA818_AUDIO_SAMPLE_SIZE];
};

/*
 * DESIGN NOTE: Global singleton audio context
 *
 * The current implementation uses a single global audio_ctx, which means only
 * one SA818 device can stream audio at a time. This is acceptable for the
 * current use case (single radio system).
 *
 * If support for multiple SA818 radios is needed in the future, this should be
 * refactored to store the audio context in the device's runtime data structure
 * (struct sa818_data), similar to how other device-specific state is managed.
 * This would require:
 * 1. Adding audio_stream_ctx to struct sa818_data
 * 2. Updating all API functions to access ctx via dev->data
 * 3. Ensuring proper initialization/cleanup per device instance
 */

/* Static mutex for audio context - initialized at compile time */
K_MUTEX_DEFINE(audio_ctx_mutex);

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

  k_mutex_lock(&audio_ctx_mutex, K_FOREVER);
  bool streaming = ctx->streaming;
  k_mutex_unlock(&audio_ctx_mutex);

  if (!streaming) {
    return;
  }

  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(ctx->dev->config);
  struct sa818_data *data = static_cast<struct sa818_data *>(ctx->dev->data);

  /* Protect access to audio enable flags */
  k_mutex_lock(&data->lock, K_FOREVER);
  bool tx_enabled = data->audio_tx_enabled;
  bool rx_enabled = data->audio_rx_enabled;
  k_mutex_unlock(&data->lock);

  /* Process TX: Get audio from application -> DAC */
  if (ctx->callbacks.tx_request && tx_enabled) {
    size_t bytes = ctx->callbacks.tx_request(ctx->dev, ctx->tx_buffer, sizeof(ctx->tx_buffer), ctx->callbacks.user_data);

    /* Process all complete samples in the TX buffer */
    size_t samples = bytes / SA818_AUDIO_SAMPLE_SIZE;
    for (size_t i = 0; i < samples; ++i) {
      const uint8_t *sample_ptr = &ctx->tx_buffer[i * SA818_AUDIO_SAMPLE_SIZE];
      int16_t pcm_sample = (int16_t)((sample_ptr[1] << 8) | sample_ptr[0]);
      /* Scale signed 16-bit (-32768 to 32767) to unsigned DAC range */
      uint32_t dac_value = ((uint32_t)(pcm_sample + 32768) << (cfg->audio_out_resolution - 16));
      dac_write_value(cfg->audio_out_dev, cfg->audio_out_channel, dac_value);
    }
  }

  /* Process RX: ADC -> Application callback */
  if (ctx->callbacks.rx_data && rx_enabled) {
    /* Read ADC */
    uint16_t adc_sequence_buf[1];
    struct adc_sequence sequence = {};

    adc_sequence_init_dt(&cfg->audio_in, &sequence);
    sequence.buffer = adc_sequence_buf;
    sequence.buffer_size = sizeof(adc_sequence_buf);

    int ret = adc_read_dt(&cfg->audio_in, &sequence);
    if (ret == 0) {
      /* Convert ADC value to 16-bit PCM.
       * We request 16-bit resolution from the ADC driver, so the sample is in
       * the full unsigned 16-bit range (0-65535). Map this to signed 16-bit
       * PCM (-32768 to 32767) by subtracting the midpoint.
       */
      uint16_t adc_value = adc_sequence_buf[0];
      int16_t pcm_sample = (int16_t)((int32_t)adc_value - 32768);

      ctx->rx_buffer[0] = (uint8_t)(pcm_sample & 0xFF);
      ctx->rx_buffer[1] = (uint8_t)((pcm_sample >> 8) & 0xFF);

      /* Call application callback */
      ctx->callbacks.rx_data(ctx->dev, ctx->rx_buffer, SA818_AUDIO_SAMPLE_SIZE, ctx->callbacks.user_data);
    }
  }

  /* Reschedule based on sample rate - check streaming flag again under mutex */
  k_mutex_lock(&audio_ctx_mutex, K_FOREVER);
  if (ctx->streaming) {
    uint32_t period_us = 1000000 / ctx->format.sample_rate;
    k_work_reschedule(&ctx->audio_work, K_USEC(period_us));
  }
  k_mutex_unlock(&audio_ctx_mutex);
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

  /* Initialize work queue once during registration */
  k_work_init_delayable(&audio_ctx.audio_work, audio_stream_work_handler);

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

  /* Start streaming */
  k_mutex_lock(&audio_ctx_mutex, K_FOREVER);
  audio_ctx.streaming = true;
  k_mutex_unlock(&audio_ctx_mutex);

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

  k_mutex_lock(&audio_ctx_mutex, K_FOREVER);
  audio_ctx.streaming = false;
  k_mutex_unlock(&audio_ctx_mutex);

  /* Ensure work is fully stopped before returning */
  struct k_work_sync sync;
  k_work_cancel_delayable_sync(&audio_ctx.audio_work, &sync);

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
