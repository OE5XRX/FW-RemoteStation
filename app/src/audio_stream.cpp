/**
 * @file audio_stream.cpp
 * @brief Generic hardware-timed audio streaming bridge. See audio_stream.h.
 *
 * Bridges application audio callbacks to the hardware-timed capture/playback
 * backend (the analog-audio-in / analog-audio-out TIM+ADC/DAC+DMA modules). It
 * is radio-agnostic: the @p dev handle is opaque and only passed back to the
 * callbacks as context.
 *
 * @copyright Copyright (c) 2026 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#include "audio_stream.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* Require the DT node to be status=okay AND the driver Kconfig on. The device
 * is only instantiated via DT_INST_FOREACH_STATUS_OKAY, so an existing-but-
 * disabled node (or CONFIG_ANALOG_AUDIO_{IN,OUT}=n) has no device symbol and
 * DEVICE_DT_GET() would fail to link. Use HAS_STATUS(okay), not NODE_EXISTS. */
#if DT_NODE_HAS_STATUS(DT_NODELABEL(audio_in), okay) && IS_ENABLED(CONFIG_ANALOG_AUDIO_IN)
#include <oe5xrx/audio/analog_audio_in.h>
#define AUDIO_STREAM_HAVE_AAI 1
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(audio_out), okay) && IS_ENABLED(CONFIG_ANALOG_AUDIO_OUT)
#include <oe5xrx/audio/analog_audio_out.h>
#define AUDIO_STREAM_HAVE_AAO 1
#endif

LOG_MODULE_REGISTER(audio_stream, LOG_LEVEL_INF);

#define AUDIO_STREAM_SAMPLE_SIZE 2 /* 16-bit = 2 bytes */

/** Audio streaming context. */
struct audio_stream_ctx {
  const struct device *dev;
  struct audio_stream_callbacks callbacks;
  struct audio_format format;
  bool streaming;
};

/*
 * DESIGN NOTE: single global streaming context — only one audio stream can be
 * active at a time, which fits the single-radio system. Storing the context per
 * device would be needed to support several concurrent streams.
 */

/* Static mutex for the streaming context - initialized at compile time */
K_MUTEX_DEFINE(audio_stream_mutex);

static struct audio_stream_ctx audio_ctx;

#ifdef AUDIO_STREAM_HAVE_AAO
/* Hardware-timed TX playback pull-source for the analog-audio-out module. Runs
 * in that module's workqueue thread (thread context, may take a mutex). Sources
 * signed 16-bit PCM from the application via the tx_request callback. Returning
 * fewer than @p max samples (or 0 when there is no data) makes the module emit
 * mid-scale silence for the shortfall; the consumer gates whether any data is
 * available. Host PCM is little-endian, matching the Cortex-M, so the byte
 * buffer maps directly onto int16 samples with no swap. */
static size_t audio_stream_tx_src(int16_t *dst, size_t max, void *user) {
  struct audio_stream_ctx *ctx = static_cast<struct audio_stream_ctx *>(user);

  if (!ctx->callbacks.tx_request) {
    return 0;
  }
  size_t bytes = ctx->callbacks.tx_request(ctx->dev, reinterpret_cast<uint8_t *>(dst), max * AUDIO_STREAM_SAMPLE_SIZE, ctx->callbacks.user_data);
  /* Defend against a callback that returns more than requested; the division to
   * whole samples already rounds a stray odd byte down. */
  if (bytes > max * AUDIO_STREAM_SAMPLE_SIZE) {
    bytes = max * AUDIO_STREAM_SAMPLE_SIZE;
  }
  return bytes / AUDIO_STREAM_SAMPLE_SIZE;
}
#endif

#ifdef AUDIO_STREAM_HAVE_AAI
/* Hardware-timed RX samples from the analog-audio-in module. Runs in the module's
 * workqueue thread (not an ISR), so forwarding through rx_data (which may take a
 * mutex) is safe. */
static void audio_stream_on_rx_samples(const int16_t *samples, size_t count, void *user) {
  struct audio_stream_ctx *ctx = static_cast<struct audio_stream_ctx *>(user);

  if (ctx->callbacks.rx_data) {
    ctx->callbacks.rx_data(ctx->dev, reinterpret_cast<const uint8_t *>(samples), count * AUDIO_STREAM_SAMPLE_SIZE, ctx->callbacks.user_data);
  }
}
#endif

int audio_stream_register(const struct device *dev, const struct audio_stream_callbacks *callbacks) {
  if (!dev || !callbacks) {
    return -EINVAL;
  }

  /* Guard the shared context and refuse to swap callbacks under a running
   * stream (the backend threads read ctx->callbacks/ctx->dev while active). */
  k_mutex_lock(&audio_stream_mutex, K_FOREVER);
  if (audio_ctx.streaming) {
    k_mutex_unlock(&audio_stream_mutex);
    return -EBUSY;
  }
  audio_ctx.dev = dev;
  audio_ctx.callbacks = *callbacks;
  k_mutex_unlock(&audio_stream_mutex);

  LOG_INF("Audio callbacks registered");
  return 0;
}

int audio_stream_start(const struct device *dev, const struct audio_format *format) {
  if (!dev || !format) {
    return -EINVAL;
  }

  /* Hold the stream mutex across the whole start sequence — the state flip AND
   * the backend bring-up. Releasing it before starting the backends would open
   * a window where a concurrent stop() clears streaming while we are still
   * bringing hardware up, leaving the backends running with streaming == false.
   * The backend callbacks (tx_src / on_rx_samples) do not take this mutex, so
   * holding it across analog_audio_*_start() cannot deadlock. */
  k_mutex_lock(&audio_stream_mutex, K_FOREVER);
  if (audio_ctx.dev != dev) {
    /* start must run against the context bound by audio_stream_register();
     * a mismatch (or a start without a prior register, dev == NULL) would
     * later trip the dev checks in stop()/get_format(). */
    k_mutex_unlock(&audio_stream_mutex);
    LOG_ERR("audio_stream_start dev does not match registered context");
    return -EINVAL;
  }
  if (audio_ctx.streaming) {
    k_mutex_unlock(&audio_stream_mutex);
    LOG_WRN("Audio streaming already active");
    return 0;
  }
  audio_ctx.format = *format;
  audio_ctx.streaming = true;

  /* Count backends that actually came up. A single backend failing only
   * degrades that direction (RX-only or TX-only is still useful), so we keep
   * streaming. But if *no* backend is driving the callbacks — none compiled in,
   * or every one failed — then reporting success would be a lie: the rings
   * would never be drained/filled. In that case roll back and fail with
   * -ENODEV. */
  int started = 0;

#ifdef AUDIO_STREAM_HAVE_AAO
  /* Start the hardware-timed TX playback module; it pulls PCM via the source
   * callback. A failure only disables TX playback (RX still works), so surface
   * it loudly rather than fail the whole stream. */
  const struct device *aao_dev = DEVICE_DT_GET(DT_NODELABEL(audio_out));
  if (!device_is_ready(aao_dev)) {
    LOG_ERR("analog-audio-out device not ready (TX playback unavailable)");
  } else {
    int aao_ret = analog_audio_out_start(aao_dev, audio_stream_tx_src, &audio_ctx);
    if (aao_ret < 0) {
      LOG_ERR("analog-audio-out start failed: %d (TX playback unavailable)", aao_ret);
    } else {
      started++;
    }
  }
#endif

#ifdef AUDIO_STREAM_HAVE_AAI
  /* Start the hardware-timed RX capture module; it delivers PCM via the callback.
   * A failure only disables RX capture (TX still works), so surface it loudly
   * rather than fail the whole stream. Verify the device initialised before use
   * (per the driver-layer device_is_ready() convention) so a failed init cannot
   * leave analog_audio_in_start() operating on uninitialised runtime state. */
  const struct device *aai_dev = DEVICE_DT_GET(DT_NODELABEL(audio_in));
  if (!device_is_ready(aai_dev)) {
    LOG_ERR("analog-audio-in device not ready (RX capture unavailable)");
  } else {
    int aai_ret = analog_audio_in_start(aai_dev, audio_stream_on_rx_samples, &audio_ctx);
    if (aai_ret < 0) {
      LOG_ERR("analog-audio-in start failed: %d (RX capture unavailable)", aai_ret);
    } else {
      started++;
    }
  }
#endif

  if (started == 0) {
    audio_ctx.streaming = false;
    k_mutex_unlock(&audio_stream_mutex);
    LOG_ERR("No audio backend available; not streaming");
    return -ENODEV;
  }

  k_mutex_unlock(&audio_stream_mutex);
  LOG_INF("Audio streaming started: %u Hz, %u-bit, %u ch", format->sample_rate, format->bit_depth, format->channels);

  return 0;
}

int audio_stream_stop(const struct device *dev) {
  if (!dev) {
    return -EINVAL;
  }

  /* Hold the mutex across the state flip AND the backend teardown so stop() is
   * fully serialized against start() (see the note there) — otherwise the two
   * could interleave and leave a backend running with streaming == false. */
  k_mutex_lock(&audio_stream_mutex, K_FOREVER);
  if (audio_ctx.dev != dev) {
    /* Not the registered/active context — refuse to stop someone else's stream. */
    k_mutex_unlock(&audio_stream_mutex);
    return -EINVAL;
  }
  audio_ctx.streaming = false;

#ifdef AUDIO_STREAM_HAVE_AAO
  /* Consume the result: the analog_audio_* stop functions are warn_unused_result,
   * and GCC's attribute (unlike [[nodiscard]]) is NOT silenced by a (void) cast. */
  int aao_stop_ret = analog_audio_out_stop(DEVICE_DT_GET(DT_NODELABEL(audio_out)));
  if (aao_stop_ret < 0) {
    LOG_WRN("analog-audio-out stop returned %d", aao_stop_ret);
  }
#endif

#ifdef AUDIO_STREAM_HAVE_AAI
  int aai_stop_ret = analog_audio_in_stop(DEVICE_DT_GET(DT_NODELABEL(audio_in)));
  if (aai_stop_ret < 0) {
    LOG_WRN("analog-audio-in stop returned %d", aai_stop_ret);
  }
#endif

  k_mutex_unlock(&audio_stream_mutex);
  LOG_INF("Audio streaming stopped");
  return 0;
}

int audio_stream_get_format(const struct device *dev, struct audio_format *format) {
  if (!dev || !format) {
    return -EINVAL;
  }

  k_mutex_lock(&audio_stream_mutex, K_FOREVER);
  if (audio_ctx.dev != dev) {
    k_mutex_unlock(&audio_stream_mutex);
    return -EINVAL;
  }
  *format = audio_ctx.format;
  k_mutex_unlock(&audio_stream_mutex);
  return 0;
}
