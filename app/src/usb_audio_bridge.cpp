/**
 * @file usb_audio_bridge.cpp
 * @brief USB Audio Bridge for SA818
 *
 * Application-level integration between USB Audio Class 2 (UAC2) and
 * SA818 audio streaming interface. Connects USB host audio to SA818 radio.
 *
 * This is application code, not part of the SA818 driver.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#include <sa818/sa818.h>
#include <sa818/sa818_audio_stream.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>

extern "C" {
#include <zephyr/usb/class/usbd_uac2.h>
#include <zephyr/usb/usbd.h>
}

LOG_MODULE_REGISTER(usb_audio_bridge, LOG_LEVEL_INF);

/* Audio configuration */
#define AUDIO_SAMPLE_RATE_HZ 8000
#define AUDIO_SAMPLE_SIZE_BYTES 2 /* 16-bit PCM */
#define AUDIO_CHANNELS 1          /* Mono */
#define AUDIO_BYTES_PER_SAMPLE (AUDIO_SAMPLE_SIZE_BYTES * AUDIO_CHANNELS)

/* USB Audio timing (Full-Speed: 1ms SOF, 8 samples/frame @ 8kHz) */
#define USB_SAMPLES_PER_SOF 8
#define USB_BYTES_PER_SOF (USB_SAMPLES_PER_SOF * AUDIO_BYTES_PER_SAMPLE)

/* Ring buffer sizes (must be power of 2) */
#define TX_RING_SIZE 512 /* USB -> SA818 (256 samples = 32ms) */
#define RX_RING_SIZE 512 /* SA818 -> USB (256 samples = 32ms) */

/* USB buffer pool */
#define USB_BUF_COUNT 8
#define USB_BUF_SIZE 32 /* 16 samples max per SOF */

/* Terminal IDs from device tree */
#define USB_OUT_TERMINAL_ID 1 /* USB -> SA818 TX */
#define USB_IN_TERMINAL_ID 4  /* SA818 RX -> USB */

/**
 * @brief USB Audio Bridge context
 */
struct usb_audio_bridge_ctx {
  const struct device *sa818_dev;
  const struct device *uac2_dev;

  /* Ring buffers */
  struct ring_buf tx_ring; /* USB OUT -> SA818 TX */
  struct ring_buf rx_ring; /* SA818 RX -> USB IN */
  uint8_t tx_ring_buf[TX_RING_SIZE];
  uint8_t rx_ring_buf[RX_RING_SIZE];

  /* USB buffer pool */
  uint8_t usb_buf_pool[USB_BUF_COUNT][USB_BUF_SIZE] __aligned(UDC_BUF_ALIGN);
  uint8_t usb_buf_idx;

  /* Synchronization */
  struct k_mutex lock;

  /* Status */
  bool tx_enabled; /* USB OUT terminal active */
  bool rx_enabled; /* USB IN terminal active */
};

static struct usb_audio_bridge_ctx bridge_ctx;

/* Forward declarations */
static void usb_in_thread_func(void *p1, void *p2, void *p3);

/**
 * @brief SA818 TX audio request callback
 *
 * SA818 driver calls this when it needs audio samples for transmission
 */
static size_t sa818_tx_request_cb(const struct device *dev, uint8_t *buffer, size_t size, void *user_data) {
  struct usb_audio_bridge_ctx *ctx = (struct usb_audio_bridge_ctx *)user_data;

  ARG_UNUSED(dev);

  if (!ctx->tx_enabled) {
    return 0;
  }

  /* Pull audio from TX ring buffer (USB OUT data) */
  k_mutex_lock(&ctx->lock, K_FOREVER);
  uint32_t bytes_read = ring_buf_get(&ctx->tx_ring, buffer, size);
  k_mutex_unlock(&ctx->lock);

  return bytes_read;
}

/**
 * @brief SA818 RX audio data callback
 *
 * SA818 driver calls this when received audio samples are available
 */
static void sa818_rx_data_cb(const struct device *dev, const uint8_t *buffer, size_t size, void *user_data) {
  struct usb_audio_bridge_ctx *ctx = (struct usb_audio_bridge_ctx *)user_data;

  ARG_UNUSED(dev);

  if (!ctx->rx_enabled) {
    return;
  }

  /* Push audio to RX ring buffer (for USB IN) */
  k_mutex_lock(&ctx->lock, K_FOREVER);
  uint32_t bytes_put = ring_buf_put(&ctx->rx_ring, buffer, size);
  k_mutex_unlock(&ctx->lock);

  if (bytes_put < size) {
    LOG_WRN("RX ring buffer overflow: %zu/%zu bytes dropped", size - bytes_put, size);
  }
}

/**
 * @brief UAC2 SOF (Start of Frame) callback
 */
static void uac2_sof_cb(const struct device *dev, void *user_data) {
  ARG_UNUSED(dev);
  ARG_UNUSED(user_data);
  /* SOF occurs every 1ms - can be used for timing if needed */
}

/**
 * @brief UAC2 Terminal update callback
 */
static void uac2_terminal_update_cb(const struct device *dev, uint8_t terminal, bool enabled, bool microframes, void *user_data) {
  struct usb_audio_bridge_ctx *ctx = (struct usb_audio_bridge_ctx *)user_data;

  ARG_UNUSED(dev);
  ARG_UNUSED(microframes);

  k_mutex_lock(&ctx->lock, K_FOREVER);

  if (terminal == USB_OUT_TERMINAL_ID) {
    ctx->tx_enabled = enabled;
    LOG_INF("USB OUT (TX) terminal %s", enabled ? "enabled" : "disabled");

    if (!enabled) {
      ring_buf_reset(&ctx->tx_ring);
    }
  } else if (terminal == USB_IN_TERMINAL_ID) {
    ctx->rx_enabled = enabled;
    LOG_INF("USB IN (RX) terminal %s", enabled ? "enabled" : "disabled");

    if (!enabled) {
      ring_buf_reset(&ctx->rx_ring);
    }
  }

  k_mutex_unlock(&ctx->lock);
}

/**
 * @brief UAC2 Get receive buffer callback
 */
static void *uac2_get_recv_buf(const struct device *dev, uint8_t terminal, uint16_t size, void *user_data) {
  struct usb_audio_bridge_ctx *ctx = (struct usb_audio_bridge_ctx *)user_data;

  ARG_UNUSED(dev);

  if (terminal != USB_OUT_TERMINAL_ID || !ctx->tx_enabled) {
    return NULL;
  }

  if (size > USB_BUF_SIZE) {
    LOG_ERR("Requested buffer size %u exceeds max %u", size, USB_BUF_SIZE);
    return NULL;
  }

  /* Return next buffer from pool */
  void *buf = ctx->usb_buf_pool[ctx->usb_buf_idx];
  ctx->usb_buf_idx = (ctx->usb_buf_idx + 1) % USB_BUF_COUNT;

  return buf;
}

/**
 * @brief UAC2 Data received callback
 */
static void uac2_data_recv_cb(const struct device *dev, uint8_t terminal, void *buf, uint16_t size, void *user_data) {
  struct usb_audio_bridge_ctx *ctx = (struct usb_audio_bridge_ctx *)user_data;

  ARG_UNUSED(dev);

  if (terminal != USB_OUT_TERMINAL_ID || !ctx->tx_enabled) {
    return;
  }

  /* Push received USB audio to TX ring buffer */
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
 */
static void uac2_buf_release_cb(const struct device *dev, uint8_t terminal, void *buf, void *user_data) {
  ARG_UNUSED(dev);
  ARG_UNUSED(terminal);
  ARG_UNUSED(buf);
  ARG_UNUSED(user_data);

  /* Buffer is from our pool, no need to free */
}

/* UAC2 operations structure */
static const struct uac2_ops uac2_ops = {
    .sof_cb = uac2_sof_cb,
    .terminal_update_cb = uac2_terminal_update_cb,
    .get_recv_buf = uac2_get_recv_buf,
    .data_recv_cb = uac2_data_recv_cb,
    .buf_release_cb = uac2_buf_release_cb,
};

/**
 * @brief USB IN streaming thread
 */
static void usb_in_thread_func(void *p1, void *p2, void *p3) {
  struct usb_audio_bridge_ctx *ctx = (struct usb_audio_bridge_ctx *)p1;

  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  while (true) {
    k_msleep(1); /* Run at ~1kHz (USB SOF rate) */

    if (!ctx->rx_enabled) {
      continue;
    }

    k_mutex_lock(&ctx->lock, K_FOREVER);

    /* Check if we have enough data to send */
    if (ring_buf_size_get(&ctx->rx_ring) >= USB_BYTES_PER_SOF) {
      /* Allocate buffer from pool */
      uint8_t buf_idx = ctx->usb_buf_idx;
      ctx->usb_buf_idx = (ctx->usb_buf_idx + 1) % USB_BUF_COUNT;
      void *buf = ctx->usb_buf_pool[buf_idx];

      /* Pull data from RX ring buffer */
      uint32_t bytes_read = ring_buf_get(&ctx->rx_ring, (uint8_t *)buf, USB_BYTES_PER_SOF);

      k_mutex_unlock(&ctx->lock);

      if (bytes_read > 0) {
        /* Send to USB host */
        int ret = usbd_uac2_send(ctx->uac2_dev, USB_IN_TERMINAL_ID, buf, bytes_read);
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

K_THREAD_DEFINE(usb_in_tid, 1024, usb_in_thread_func, &bridge_ctx, NULL, NULL, 7, 0, 0);

/**
 * @brief Initialize USB Audio Bridge
 */
int usb_audio_bridge_init(const struct device *sa818_dev, const struct device *uac2_dev) {
  struct usb_audio_bridge_ctx *ctx = &bridge_ctx;

  if (ctx->sa818_dev != NULL) {
    LOG_WRN("USB Audio Bridge already initialized");
    return 0;
  }

  ctx->sa818_dev = sa818_dev;
  ctx->uac2_dev = uac2_dev;

  /* Initialize ring buffers */
  ring_buf_init(&ctx->tx_ring, sizeof(ctx->tx_ring_buf), ctx->tx_ring_buf);
  ring_buf_init(&ctx->rx_ring, sizeof(ctx->rx_ring_buf), ctx->rx_ring_buf);

  /* Initialize synchronization */
  k_mutex_init(&ctx->lock);

  /* Reset state */
  ctx->tx_enabled = false;
  ctx->rx_enabled = false;
  ctx->usb_buf_idx = 0;

  /* Register UAC2 callbacks */
  usbd_uac2_set_ops(uac2_dev, &uac2_ops, ctx);

  /* Register SA818 audio callbacks */
  struct sa818_audio_callbacks sa818_cbs = {
      .tx_request = sa818_tx_request_cb,
      .rx_data = sa818_rx_data_cb,
      .user_data = ctx,
  };

  sa818_result ret = sa818_audio_stream_register(sa818_dev, &sa818_cbs);
  if (ret != SA818_OK) {
    LOG_ERR("Failed to register SA818 audio callbacks: %d", ret);
    return -EIO;
  }

  /* Start SA818 audio streaming */
  struct sa818_audio_format format = {
      .sample_rate = AUDIO_SAMPLE_RATE_HZ,
      .bit_depth = 16,
      .channels = 1,
  };

  ret = sa818_audio_stream_start(sa818_dev, &format);
  if (ret != SA818_OK) {
    LOG_ERR("Failed to start SA818 audio streaming: %d", ret);
    return -EIO;
  }

  LOG_INF("USB Audio Bridge initialized (8kHz, 16-bit, mono)");
  LOG_INF("  USB OUT -> TX Ring (%u bytes) -> SA818 TX", TX_RING_SIZE);
  LOG_INF("  SA818 RX -> RX Ring (%u bytes) -> USB IN", RX_RING_SIZE);

  return 0;
}
