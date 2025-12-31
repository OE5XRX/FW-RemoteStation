/**
 * @file sa818_usb_audio.cpp
 * @brief SA818 USB Audio Bridge Implementation
 *
 * Implements bidirectional audio streaming between USB Audio Class 2 (UAC2)
 * and SA818 radio module using ring buffers and asynchronous processing.
 *
 * Audio Flow:
 * - USB OUT (Playback) -> Ring Buffer -> DAC -> SA818 TX
 * - SA818 RX -> ADC -> Ring Buffer -> USB IN (Capture)
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#include "sa818_priv.h"
#include <sa818/sa818_usb_audio.h>

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/usb/class/usbd_uac2.h>
#include <zephyr/usb/usbd.h>

LOG_MODULE_REGISTER(sa818_usb_audio, LOG_LEVEL_INF);

/* Audio configuration matching UAC2 device tree */
#define SA818_SAMPLE_RATE_HZ 8000
#define SA818_SAMPLE_SIZE_BYTES 2      /* 16-bit PCM */
#define SA818_CHANNELS 1               /* Mono */
#define SA818_BYTES_PER_SAMPLE (SA818_SAMPLE_SIZE_BYTES * SA818_CHANNELS)

/* USB Audio timing (Full-Speed: 1ms SOF, 8 samples/frame @ 8kHz) */
#define SA818_SAMPLES_PER_SOF 8
#define SA818_BYTES_PER_SOF (SA818_SAMPLES_PER_SOF * SA818_BYTES_PER_SAMPLE)

/* Ring buffer sizes (must be power of 2) */
#define SA818_TX_RING_SIZE 512 /* USB -> SA818 (256 samples = 32ms @ 8kHz) */
#define SA818_RX_RING_SIZE 512 /* SA818 -> USB (256 samples = 32ms @ 8kHz) */

/* USB buffer pool */
#define SA818_USB_BUF_COUNT 8
#define SA818_USB_BUF_SIZE 32 /* 16 samples max per SOF */

/* Terminal IDs from device tree */
#define SA818_USB_OUT_TERMINAL_ID 1 /* USB -> SA818 TX */
#define SA818_USB_IN_TERMINAL_ID 4  /* SA818 RX -> USB */

/**
 * @brief USB Audio context for SA818
 */
struct sa818_usb_audio_ctx {
  const struct device *sa818_dev;
  const struct device *uac2_dev;

  /* Ring buffers for audio streaming */
  struct ring_buf tx_ring; /* USB OUT -> DAC */
  struct ring_buf rx_ring; /* ADC -> USB IN */
  uint8_t tx_ring_buf[SA818_TX_RING_SIZE];
  uint8_t rx_ring_buf[SA818_RX_RING_SIZE];

  /* USB buffer pool (aligned for DMA) */
  uint8_t usb_buf_pool[SA818_USB_BUF_COUNT][SA818_USB_BUF_SIZE] __aligned(UDC_BUF_ALIGN);
  uint8_t usb_buf_idx;

  /* Audio processing work */
  struct k_work_delayable audio_work;
  struct k_mutex lock;

  /* Status flags */
  bool tx_enabled; /* USB OUT terminal active */
  bool rx_enabled; /* USB IN terminal active */
  bool streaming;  /* Audio processing active */
};

static struct sa818_usb_audio_ctx usb_audio_ctx;

/**
 * @brief Audio processing work handler
 *
 * Periodically processes audio data:
 * - Pulls samples from TX ring buffer and writes to DAC
 * - Reads samples from ADC and pushes to RX ring buffer
 */
static void audio_work_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct sa818_usb_audio_ctx *ctx = CONTAINER_OF(dwork, struct sa818_usb_audio_ctx, audio_work);

  if (!ctx->streaming) {
    return;
  }

  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(ctx->sa818_dev->config);
  k_mutex_lock(&ctx->lock, K_FOREVER);

  /* Process TX: Ring buffer -> DAC */
  if (ctx->tx_enabled && ring_buf_size_get(&ctx->tx_ring) >= SA818_BYTES_PER_SAMPLE) {
    uint8_t sample_buf[SA818_BYTES_PER_SAMPLE];
    uint32_t bytes_read = ring_buf_get(&ctx->tx_ring, sample_buf, SA818_BYTES_PER_SAMPLE);

    if (bytes_read == SA818_BYTES_PER_SAMPLE) {
      /* Convert 16-bit PCM to DAC value */
      int16_t pcm_sample = (int16_t)((sample_buf[1] << 8) | sample_buf[0]);
      /* Scale signed 16-bit (-32768 to 32767) to unsigned DAC range */
      uint32_t dac_value = ((uint32_t)(pcm_sample + 32768) << (cfg->audio_out_resolution - 16));
      dac_write_value(cfg->audio_out_dev, cfg->audio_out_channel, dac_value);
    }
  }

  /* Process RX: ADC -> Ring buffer */
  if (ctx->rx_enabled && ring_buf_space_get(&ctx->rx_ring) >= SA818_BYTES_PER_SAMPLE) {
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

      uint8_t sample_buf[SA818_BYTES_PER_SAMPLE];
      sample_buf[0] = (uint8_t)(pcm_sample & 0xFF);
      sample_buf[1] = (uint8_t)((pcm_sample >> 8) & 0xFF);

      ring_buf_put(&ctx->rx_ring, sample_buf, SA818_BYTES_PER_SAMPLE);
    }
  }

  k_mutex_unlock(&ctx->lock);

  /* Reschedule: Process at 8kHz sample rate (125us period) */
  k_work_reschedule(&ctx->audio_work, K_USEC(125));
}

/**
 * @brief UAC2 SOF (Start of Frame) callback
 */
static void uac2_sof_cb(const struct device *dev, void *user_data) {
  /* SOF occurs every 1ms - can be used for timing synchronization if needed */
  ARG_UNUSED(dev);
  ARG_UNUSED(user_data);
}

/**
 * @brief UAC2 Terminal update callback
 *
 * Called when host enables/disables audio terminals
 */
static void uac2_terminal_update_cb(const struct device *dev, uint8_t terminal, bool enabled, bool microframes, void *user_data) {
  struct sa818_usb_audio_ctx *ctx = (struct sa818_usb_audio_ctx *)user_data;

  ARG_UNUSED(dev);
  ARG_UNUSED(microframes);

  k_mutex_lock(&ctx->lock, K_FOREVER);

  if (terminal == SA818_USB_OUT_TERMINAL_ID) {
    ctx->tx_enabled = enabled;
    LOG_INF("USB OUT (TX) terminal %s", enabled ? "enabled" : "disabled");
  } else if (terminal == SA818_USB_IN_TERMINAL_ID) {
    ctx->rx_enabled = enabled;
    LOG_INF("USB IN (RX) terminal %s", enabled ? "enabled" : "disabled");
  }

  /* Start/stop audio processing based on terminal states */
  if ((ctx->tx_enabled || ctx->rx_enabled) && !ctx->streaming) {
    ctx->streaming = true;
    k_work_reschedule(&ctx->audio_work, K_MSEC(1));
    LOG_INF("Audio streaming started");
  } else if (!ctx->tx_enabled && !ctx->rx_enabled && ctx->streaming) {
    ctx->streaming = false;
    k_work_cancel_delayable(&ctx->audio_work);
    ring_buf_reset(&ctx->tx_ring);
    ring_buf_reset(&ctx->rx_ring);
    LOG_INF("Audio streaming stopped");
  }

  k_mutex_unlock(&ctx->lock);
}

/**
 * @brief UAC2 Get receive buffer callback
 *
 * Provides buffer for USB OUT data (host -> device)
 */
static void *uac2_get_recv_buf(const struct device *dev, uint8_t terminal, uint16_t size, void *user_data) {
  struct sa818_usb_audio_ctx *ctx = (struct sa818_usb_audio_ctx *)user_data;

  ARG_UNUSED(dev);

  if (terminal != SA818_USB_OUT_TERMINAL_ID || !ctx->tx_enabled) {
    return NULL;
  }

  if (size > SA818_USB_BUF_SIZE) {
    LOG_ERR("Requested buffer size %u exceeds max %u", size, SA818_USB_BUF_SIZE);
    return NULL;
  }

  /* Return next buffer from pool */
  void *buf = ctx->usb_buf_pool[ctx->usb_buf_idx];
  ctx->usb_buf_idx = (ctx->usb_buf_idx + 1) % SA818_USB_BUF_COUNT;

  return buf;
}

/**
 * @brief UAC2 Data received callback
 *
 * Called when USB OUT data is available (host -> device)
 */
static void uac2_data_recv_cb(const struct device *dev, uint8_t terminal, void *buf, uint16_t size, void *user_data) {
  struct sa818_usb_audio_ctx *ctx = (struct sa818_usb_audio_ctx *)user_data;

  ARG_UNUSED(dev);

  if (terminal != SA818_USB_OUT_TERMINAL_ID || !ctx->tx_enabled) {
    return;
  }

  /* Push received audio data to TX ring buffer */
  k_mutex_lock(&ctx->lock, K_FOREVER);
  uint32_t bytes_put = ring_buf_put(&ctx->tx_ring, (uint8_t *)buf, size);
  k_mutex_unlock(&ctx->lock);

  if (bytes_put < size) {
    LOG_WRN("TX ring buffer overflow: %u/%u bytes dropped", size - bytes_put, size);
  }

  LOG_DBG("USB OUT: %u bytes -> TX ring", bytes_put);
}

/**
 * @brief UAC2 Buffer release callback
 *
 * Called when USB IN buffer can be reused (device -> host)
 */
static void uac2_buf_release_cb(const struct device *dev, uint8_t terminal, void *buf, void *user_data) {
  ARG_UNUSED(dev);
  ARG_UNUSED(terminal);
  ARG_UNUSED(buf);
  ARG_UNUSED(user_data);

  /* Buffer is from our pool, no need to free */
}

/* UAC2 operations structure */
static const struct uac2_ops sa818_uac2_ops = {
    .sof_cb = uac2_sof_cb,
    .terminal_update_cb = uac2_terminal_update_cb,
    .get_recv_buf = uac2_get_recv_buf,
    .data_recv_cb = uac2_data_recv_cb,
    .buf_release_cb = uac2_buf_release_cb,
};

/**
 * @brief USB IN streaming thread
 *
 * Sends audio data from RX ring buffer to USB host
 */
static void usb_in_thread(void *p1, void *p2, void *p3) {
  struct sa818_usb_audio_ctx *ctx = (struct sa818_usb_audio_ctx *)p1;

  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  while (true) {
    k_msleep(1); /* Run at ~1kHz (USB SOF rate) */

    if (!ctx->rx_enabled || !ctx->streaming) {
      continue;
    }

    k_mutex_lock(&ctx->lock, K_FOREVER);

    /* Check if we have enough data to send */
    if (ring_buf_size_get(&ctx->rx_ring) >= SA818_BYTES_PER_SOF) {
      /* Allocate buffer from pool */
      uint8_t buf_idx = ctx->usb_buf_idx;
      ctx->usb_buf_idx = (ctx->usb_buf_idx + 1) % SA818_USB_BUF_COUNT;
      void *buf = ctx->usb_buf_pool[buf_idx];

      /* Pull data from RX ring buffer */
      uint32_t bytes_read = ring_buf_get(&ctx->rx_ring, (uint8_t *)buf, SA818_BYTES_PER_SOF);

      k_mutex_unlock(&ctx->lock);

      if (bytes_read > 0) {
        /* Send to USB host */
        int ret = usbd_uac2_send(ctx->uac2_dev, SA818_USB_IN_TERMINAL_ID, buf, bytes_read);
        if (ret != 0) {
          LOG_WRN("USB IN send failed: %d", ret);
        } else {
          LOG_DBG("USB IN: %u bytes sent", bytes_read);
        }
      }
    } else {
      k_mutex_unlock(&ctx->lock);
    }
  }
}

K_THREAD_DEFINE(usb_in_tid, 1024, usb_in_thread, &usb_audio_ctx, NULL, NULL, 7, 0, 0);

/**
 * @brief Initialize USB Audio integration
 */
sa818_result sa818_usb_audio_init(const struct device *dev, const struct device *uac2_dev) {
  struct sa818_usb_audio_ctx *ctx = &usb_audio_ctx;

  if (ctx->sa818_dev != NULL) {
    LOG_WRN("USB Audio already initialized");
    return SA818_OK;
  }

  ctx->sa818_dev = dev;
  ctx->uac2_dev = uac2_dev;

  /* Initialize ring buffers */
  ring_buf_init(&ctx->tx_ring, sizeof(ctx->tx_ring_buf), ctx->tx_ring_buf);
  ring_buf_init(&ctx->rx_ring, sizeof(ctx->rx_ring_buf), ctx->rx_ring_buf);

  /* Initialize work queue */
  k_work_init_delayable(&ctx->audio_work, audio_work_handler);
  k_mutex_init(&ctx->lock);

  /* Reset state */
  ctx->tx_enabled = false;
  ctx->rx_enabled = false;
  ctx->streaming = false;
  ctx->usb_buf_idx = 0;

  /* Register UAC2 callbacks */
  usbd_uac2_set_ops(uac2_dev, &sa818_uac2_ops, ctx);

  LOG_INF("USB Audio initialized (8kHz, 16-bit, mono)");
  return SA818_OK;
}

/**
 * @brief Enable USB audio streaming
 */
sa818_result sa818_usb_audio_enable(const struct device *dev) {
  ARG_UNUSED(dev);

  /* Streaming is controlled automatically by UAC2 terminal callbacks */
  LOG_INF("USB Audio enabled (waiting for host to activate terminals)");
  return SA818_OK;
}

/**
 * @brief Disable USB audio streaming
 */
sa818_result sa818_usb_audio_disable(const struct device *dev) {
  struct sa818_usb_audio_ctx *ctx = &usb_audio_ctx;

  ARG_UNUSED(dev);

  k_mutex_lock(&ctx->lock, K_FOREVER);
  ctx->streaming = false;
  k_work_cancel_delayable(&ctx->audio_work);
  ring_buf_reset(&ctx->tx_ring);
  ring_buf_reset(&ctx->rx_ring);
  k_mutex_unlock(&ctx->lock);

  LOG_INF("USB Audio disabled");
  return SA818_OK;
}
