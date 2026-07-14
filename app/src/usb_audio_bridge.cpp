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

#include "feedback.h"

#include <sa818/sa818.h>
#include <sa818/sa818_audio.h>
#include <sa818/sa818_audio_stream.h>
#include <string.h>
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

/* Start draining the TX ring to the SA818 only once it is ~half full, so the
 * feedback loop has slack in both directions from the first consumed sample. */
#define TX_PREBUFFER_BYTES (TX_RING_SIZE / 2)

/* USB buffer pool */
#define USB_BUF_COUNT 8
#define USB_BUF_SIZE 32 /* 16 samples max per SOF */

/* Max bytes for one async IN isochronous packet == the IN endpoint's
 * wMaxPacketSize. The clock is free-running (not SOF-synchronized), so the UAC2
 * class sizes the endpoint for (nominal + 1) samples per frame. The per-SOF send
 * MUST NOT exceed this or the UDC emits an oversized (babble) packet. This caps
 * only the SEND; USB_BUF_SIZE (pool storage) may stay larger. */
#define USB_IN_MAX_PACKET_BYTES ((USB_SAMPLES_PER_SOF + 1) * AUDIO_BYTES_PER_SAMPLE)

/*
 * Terminal IDs the UAC2 class reports to the application callbacks.
 *
 * The class identifies each AudioStreaming interface by the entity ID of its
 * `linked-terminal` (see usbd_uac2.c: cfg->as_terminals[]). Entity IDs are
 * assigned in device-tree child order, and the CLOCK SOURCE is entity 1 -- so
 * the terminals do NOT start at 1. For fm_board.dts the order is:
 *   aclk=1, usb_out=2, sa818_tx=3, sa818_rx=4, usb_in=5.
 * The streaming interfaces link to usb_out (OUT) and usb_in (IN), so the class
 * passes those IDs to get_recv_buf()/data_recv_cb() and expects them in
 * usbd_uac2_send():
 *       * USB OUT (host -> SA818 TX)  -> usb_out terminal -> ID 2
 *       * USB IN  (SA818 RX -> host)  -> usb_in terminal  -> ID 5
 *
 * These values MUST match the actual UAC2 descriptors. If you change the
 * UAC2 audio topology (e.g. add/remove/reorder entities in the device tree
 * or Kconfig), re-check the generated descriptors and update the defines
 * and static_asserts below accordingly.
 */
#define USB_OUT_TERMINAL_ID 2 /* USB -> SA818 TX (usb_out terminal) */
#define USB_IN_TERMINAL_ID 5  /* SA818 RX -> USB (usb_in terminal) */

/* Build-time guard: force maintainers to consciously update IDs if they change. */
static_assert(USB_OUT_TERMINAL_ID == 2, "USB_OUT_TERMINAL_ID changed: verify UAC2 OUT terminal ID from "
                                        "the device tree/descriptors and update this check accordingly.");
static_assert(USB_IN_TERMINAL_ID == 5, "USB_IN_TERMINAL_ID changed: verify UAC2 IN terminal ID from "
                                       "the device tree/descriptors and update this check accordingly.");

/**
 * @brief USB Audio Bridge context
 */
struct usb_audio_bridge_ctx {
  const struct device *sa818_dev;
  const struct device *uac2_dev;

  /* Ring buffers */
  struct ring_buf tx_ring; /* USB OUT -> SA818 TX */
  struct ring_buf rx_ring; /* SA818 RX -> USB IN */
  uint8_t tx_ring_buf[TX_RING_SIZE] __aligned(UDC_BUF_ALIGN);
  uint8_t rx_ring_buf[RX_RING_SIZE] __aligned(UDC_BUF_ALIGN);

  /* USB buffer pools - separate for each direction */
  uint8_t usb_out_buf_pool[USB_BUF_COUNT][USB_BUF_SIZE] __aligned(UDC_BUF_ALIGN); /* USB OUT (receive) */
  uint8_t usb_in_buf_pool[USB_BUF_COUNT][USB_BUF_SIZE] __aligned(UDC_BUF_ALIGN);  /* USB IN (transmit) */
  uint8_t usb_out_buf_idx;
  uint8_t usb_in_buf_idx;

  /* Synchronization */
  struct k_mutex lock;

  /* Status */
  bool tx_enabled;                    /* USB OUT terminal active */
  bool rx_enabled;                    /* USB IN terminal active */
  bool tx_prebuffered;                /* TX ring reached the prebuffer threshold */
  usb_audio::BufferFeedback feedback; /* explicit feedback regulator (OUT) */
};

static struct usb_audio_bridge_ctx bridge_ctx;

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

  k_mutex_lock(&ctx->lock, K_FOREVER);

  /* Hold off draining until the ring has prebuffered ~half full. Until then
   * emit silence so the loop has slack before the SA818 starts consuming. */
  if (!ctx->tx_prebuffered) {
    if (ring_buf_size_get(&ctx->tx_ring) >= TX_PREBUFFER_BYTES) {
      ctx->tx_prebuffered = true;
    } else {
      /* Emit real PCM silence (zero samples) rather than a 0-length return:
       * the SA818 stream handler writes one DAC value per returned sample, so
       * returning 0 would leave the DAC holding its last value (a DC level)
       * instead of silence. Leave the ring untouched so it keeps prebuffering. */
      memset(buffer, 0, size);
      k_mutex_unlock(&ctx->lock);
      return size;
    }
  }

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
 *
 * All UAC2 ops callbacks (this one included) run serially on Zephyr's
 * usbd_thread, so ctx->feedback needs no separate lock of its own.
 */
static void uac2_sof_cb(const struct device *dev, void *user_data) {
  struct usb_audio_bridge_ctx *ctx = (struct usb_audio_bridge_ctx *)user_data;

  ARG_UNUSED(dev);

  /* OUT explicit feedback: keep the TX ring near half full. */
  k_mutex_lock(&ctx->lock, K_FOREVER);
  bool tx = ctx->tx_enabled;
  size_t tx_used = ring_buf_size_get(&ctx->tx_ring) / AUDIO_BYTES_PER_SAMPLE;
  k_mutex_unlock(&ctx->lock);

  if (tx) {
    ctx->feedback.update(tx_used, TX_RING_SIZE / AUDIO_BYTES_PER_SAMPLE);
  }

  /* IN capture: send whatever whole samples we have this SOF. As an async IN
   * endpoint the variable packet size itself conveys the rate; no feedback. */
  k_mutex_lock(&ctx->lock, K_FOREVER);
  bool rx = ctx->rx_enabled;
  size_t avail = ring_buf_size_get(&ctx->rx_ring);
  size_t to_send = avail - (avail % AUDIO_BYTES_PER_SAMPLE);
  if (to_send > USB_IN_MAX_PACKET_BYTES) {
    to_send = USB_IN_MAX_PACKET_BYTES;
  }

  if (rx && to_send > 0) {
    uint8_t buf_idx = ctx->usb_in_buf_idx;
    ctx->usb_in_buf_idx = (ctx->usb_in_buf_idx + 1) % USB_BUF_COUNT;
    void *buf = ctx->usb_in_buf_pool[buf_idx];
    uint32_t bytes_read = ring_buf_get(&ctx->rx_ring, (uint8_t *)buf, to_send);
    k_mutex_unlock(&ctx->lock);

    /* -EAGAIN just means the host has not drained the previous IN packet yet;
     * drop silently (rate-limited for genuinely unexpected errors) so we never
     * flood the log and starve the USB thread. */
    int ret = usbd_uac2_send(ctx->uac2_dev, USB_IN_TERMINAL_ID, buf, bytes_read);
    if (ret != 0 && ret != -EAGAIN) {
      LOG_WRN_RATELIMIT("USB IN send failed: %d", ret);
    }
  } else {
    k_mutex_unlock(&ctx->lock);
  }
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
    ctx->tx_prebuffered = false;
    ctx->feedback.reset();
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

  bool rx = ctx->rx_enabled;
  bool tx = ctx->tx_enabled;
  const struct device *sa818 = ctx->sa818_dev;

  k_mutex_unlock(&ctx->lock);

  /* Enable/disable the SA818 ADC(RX)/DAC(TX) audio paths to match the active
   * USB streams. Without this the ADC is never sampled, so rx_ring stays empty
   * and no capture data ever reaches the host. Called outside ctx->lock to keep
   * a consistent lock order with the SA818 driver's own lock. */
  if (sa818 != NULL) {
    (void)sa818_audio_enable_path(sa818, rx, tx);
  }
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

  /* Return next buffer from pool - protect with mutex */
  k_mutex_lock(&ctx->lock, K_FOREVER);
  void *buf = ctx->usb_out_buf_pool[ctx->usb_out_buf_idx];
  ctx->usb_out_buf_idx = (ctx->usb_out_buf_idx + 1) % USB_BUF_COUNT;
  k_mutex_unlock(&ctx->lock);

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

/**
 * @brief UAC2 explicit feedback callback (OUT / playback path)
 *
 * Returns the Q10.14 samples-per-SOF the host should send so the TX ring stays
 * near half full. Only the OUT input-terminal has a feedback endpoint.
 */
static uint32_t uac2_feedback_cb(const struct device *dev, uint8_t terminal, void *user_data) {
  struct usb_audio_bridge_ctx *ctx = (struct usb_audio_bridge_ctx *)user_data;

  ARG_UNUSED(dev);

  if (terminal != USB_OUT_TERMINAL_ID) {
    return 0;
  }

  return ctx->feedback.value();
}

/* UAC2 operations structure */
static const struct uac2_ops uac2_ops = {
    .sof_cb = uac2_sof_cb,
    .terminal_update_cb = uac2_terminal_update_cb,
    .get_recv_buf = uac2_get_recv_buf,
    .data_recv_cb = uac2_data_recv_cb,
    .buf_release_cb = uac2_buf_release_cb,
    .feedback_cb = uac2_feedback_cb,
};

/**
 * @brief Register UAC2 ops and prepare the bridge context.
 *
 * Must run BEFORE usbd_init() (the UAC2 class init hook rejects the config with
 * -EINVAL if the ops are not registered yet). See usb_audio_bridge.h.
 *
 * NOTE: This function must only be called once during system initialization
 * from a single thread. It is not thread-safe for concurrent calls.
 */
extern "C" int usb_audio_bridge_register_ops(const struct device *uac2_dev) {
  struct usb_audio_bridge_ctx *ctx = &bridge_ctx;

  if (uac2_dev == NULL) {
    LOG_ERR("uac2_dev is NULL");
    return -EINVAL;
  }

  if (ctx->uac2_dev != NULL) {
    LOG_WRN("USB Audio Bridge ops already registered");
    return 0;
  }

  ctx->uac2_dev = uac2_dev;

  /* Initialize ring buffers */
  ring_buf_init(&ctx->tx_ring, sizeof(ctx->tx_ring_buf), ctx->tx_ring_buf);
  ring_buf_init(&ctx->rx_ring, sizeof(ctx->rx_ring_buf), ctx->rx_ring_buf);

  /* Initialize synchronization */
  k_mutex_init(&ctx->lock);

  /* Reset state */
  ctx->tx_enabled = false;
  ctx->rx_enabled = false;
  ctx->usb_out_buf_idx = 0;
  ctx->usb_in_buf_idx = 0;
  ctx->tx_prebuffered = false;
  ctx->feedback.init(USB_SAMPLES_PER_SOF);

  /* Register UAC2 callbacks. This MUST happen before usbd_init(): the UAC2
   * class init hook returns -EINVAL ("Application did not register UAC2 ops")
   * otherwise, which fails the whole USB device init and prevents enumeration. */
  usbd_uac2_set_ops(uac2_dev, &uac2_ops, ctx);

  return 0;
}

extern "C" int usb_audio_bridge_start(const struct device *sa818_dev) {
  struct usb_audio_bridge_ctx *ctx = &bridge_ctx;

  if (sa818_dev == NULL) {
    LOG_ERR("sa818_dev is NULL");
    return -EINVAL;
  }

  if (ctx->uac2_dev == NULL) {
    LOG_ERR("usb_audio_bridge_register_ops() must be called first");
    return -EINVAL;
  }

  /* sa818_dev is read by uac2_terminal_update_cb() under ctx->lock and this
   * function runs after usbd_enable() (callbacks can fire concurrently), so the
   * check-and-set must be done under the same lock. */
  k_mutex_lock(&ctx->lock, K_FOREVER);
  if (ctx->sa818_dev != NULL) {
    k_mutex_unlock(&ctx->lock);
    LOG_WRN("USB Audio Bridge already started");
    return 0;
  }
  ctx->sa818_dev = sa818_dev;
  k_mutex_unlock(&ctx->lock);

  /* Register SA818 audio callbacks */
  struct sa818_audio_callbacks sa818_cbs = {
      .tx_request = sa818_tx_request_cb,
      .rx_data = sa818_rx_data_cb,
      .user_data = ctx,
  };

  sa818_result ret = sa818_audio_stream_register(sa818_dev, &sa818_cbs);
  if (ret != SA818_OK) {
    LOG_ERR("Failed to register SA818 audio callbacks: %d", ret);
    k_mutex_lock(&ctx->lock, K_FOREVER);
    ctx->sa818_dev = NULL;
    k_mutex_unlock(&ctx->lock);
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
    k_mutex_lock(&ctx->lock, K_FOREVER);
    ctx->sa818_dev = NULL;
    k_mutex_unlock(&ctx->lock);
    return -EIO;
  }

  /* The host may have enabled AudioStreaming alt-settings before the bridge was
   * started (uac2_terminal_update_cb then ran with ctx->sa818_dev == NULL and
   * skipped the path enable). Now that sa818_dev is set, sync the SA818 RX/TX
   * audio paths to the already-observed USB stream state so the ADC/DAC get
   * enabled without waiting for the host to toggle streaming again. */
  k_mutex_lock(&ctx->lock, K_FOREVER);
  bool rx_now = ctx->rx_enabled;
  bool tx_now = ctx->tx_enabled;
  k_mutex_unlock(&ctx->lock);
  (void)sa818_audio_enable_path(sa818_dev, rx_now, tx_now);

  LOG_INF("USB Audio Bridge started (8kHz, 16-bit, mono)");
  LOG_INF("  USB OUT -> TX Ring (%u bytes) -> SA818 TX", TX_RING_SIZE);
  LOG_INF("  SA818 RX -> RX Ring (%u bytes) -> USB IN", RX_RING_SIZE);

  return 0;
}
