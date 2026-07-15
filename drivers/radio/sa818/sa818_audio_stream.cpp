/**
 * @file sa818_audio_stream.cpp
 * @brief SA818 Audio Streaming Implementation
 *
 * Implements a generic, hardware-agnostic audio streaming interface using
 * callbacks. The actual sample timing is delegated to the hardware-timed
 * analog-audio-in (RX capture) and analog-audio-out (TX playback) modules; this
 * layer only bridges their PCM to/from the application callbacks (USB, etc.).
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#include "sa818_priv.h"

#include <sa818/sa818_audio_stream.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if DT_NODE_EXISTS(DT_NODELABEL(audio_in))
#include <oe5xrx/audio/analog_audio_in.h>
#define SA818_HAVE_AAI 1
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(audio_out))
#include <oe5xrx/audio/analog_audio_out.h>
#define SA818_HAVE_AAO 1
#endif

LOG_MODULE_REGISTER(sa818_audio_stream, LOG_LEVEL_INF);

#define SA818_AUDIO_SAMPLE_SIZE 2 /* 16-bit = 2 bytes */

/**
 * @brief Audio streaming context
 */
struct sa818_audio_stream_ctx {
  const struct device *dev;
  struct sa818_audio_callbacks callbacks;
  struct sa818_audio_format format;
  bool streaming;
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
 */

/* Static mutex for audio context - initialized at compile time */
K_MUTEX_DEFINE(audio_ctx_mutex);

static struct sa818_audio_stream_ctx audio_ctx;

#ifdef SA818_HAVE_AAO
/* Hardware-timed TX playback pull-source for the analog-audio-out module. Runs
 * in that module's workqueue thread (thread context, may take a mutex). Sources
 * signed 16-bit PCM from the application via the tx_request callback, gated by
 * the TX-enable flag. Returning fewer than @p max samples (or 0 when TX is
 * disabled / no data) makes the module emit mid-scale silence for the shortfall.
 * Host PCM is little-endian, matching the Cortex-M, so the byte buffer maps
 * directly onto int16 samples with no swap. */
static size_t sa818_aao_tx_src(int16_t *dst, size_t max, void *user) {
  struct sa818_audio_stream_ctx *ctx = static_cast<struct sa818_audio_stream_ctx *>(user);
  struct sa818_data *data = static_cast<struct sa818_data *>(ctx->dev->data);

  k_mutex_lock(&data->lock, K_FOREVER);
  bool tx_enabled = data->audio_tx_enabled;
  k_mutex_unlock(&data->lock);

  if (!tx_enabled || !ctx->callbacks.tx_request) {
    return 0;
  }
  size_t bytes = ctx->callbacks.tx_request(ctx->dev, reinterpret_cast<uint8_t *>(dst), max * SA818_AUDIO_SAMPLE_SIZE, ctx->callbacks.user_data);
  return bytes / SA818_AUDIO_SAMPLE_SIZE;
}
#endif

#ifdef SA818_HAVE_AAI
/* Hardware-timed RX samples from the analog-audio-in module. Runs in the module's
 * workqueue thread (not an ISR), so forwarding through rx_data (which may take a
 * mutex) is safe. */
static void sa818_aai_on_samples(const int16_t *samples, size_t count, void *user) {
  struct sa818_audio_stream_ctx *ctx = static_cast<struct sa818_audio_stream_ctx *>(user);

  if (ctx->callbacks.rx_data) {
    ctx->callbacks.rx_data(ctx->dev, reinterpret_cast<const uint8_t *>(samples), count * SA818_AUDIO_SAMPLE_SIZE, ctx->callbacks.user_data);
  }
}
#endif

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

  k_mutex_lock(&audio_ctx_mutex, K_FOREVER);
  audio_ctx.streaming = true;
  k_mutex_unlock(&audio_ctx_mutex);

#ifdef SA818_HAVE_AAO
  /* Start the hardware-timed TX playback module; it pulls PCM via the source
   * callback. A failure only disables TX playback (RX still works), so surface
   * it loudly rather than fail the whole stream. */
  int aao_ret = analog_audio_out_start(DEVICE_DT_GET(DT_NODELABEL(audio_out)), sa818_aao_tx_src, &audio_ctx);
  if (aao_ret < 0) {
    LOG_ERR("analog-audio-out start failed: %d (TX playback unavailable)", aao_ret);
  }
#endif

#ifdef SA818_HAVE_AAI
  /* Start the hardware-timed RX capture module; it delivers PCM via the callback.
   * A failure only disables RX capture (TX still works), so surface it loudly
   * rather than fail the whole stream. */
  int aai_ret = analog_audio_in_start(DEVICE_DT_GET(DT_NODELABEL(audio_in)), sa818_aai_on_samples, &audio_ctx);
  if (aai_ret < 0) {
    LOG_ERR("analog-audio-in start failed: %d (RX capture unavailable)", aai_ret);
  }
#endif

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

#ifdef SA818_HAVE_AAO
  (void)analog_audio_out_stop(DEVICE_DT_GET(DT_NODELABEL(audio_out)));
#endif

#ifdef SA818_HAVE_AAI
  (void)analog_audio_in_stop(DEVICE_DT_GET(DT_NODELABEL(audio_in)));
#endif

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
