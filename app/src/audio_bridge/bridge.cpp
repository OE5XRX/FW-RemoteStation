/**
 * @file bridge.cpp
 * @brief USB Audio Bridge implementation
 *
 * @copyright Copyright (c) 2026 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#include "bridge.h"

#include <sa818/sa818.h>
#include <sa818/sa818_audio_stream.h>
#include <string.h>
#include <zephyr/logging/log.h>

extern "C" {
#include <zephyr/usb/class/usbd_uac2.h>
#include <zephyr/usb/usbd.h>
}

#include "raii.h"

LOG_MODULE_REGISTER(usb_audio_bridge, LOG_LEVEL_INF);

namespace oe5xrx::audio {

// Thread stack with proper Zephyr alignment
K_THREAD_STACK_DEFINE(usb_in_thread_stack, 1024);

// ============================================================================
// UAC2 Operations Table
// ============================================================================

static uint32_t feedback_callback(const device *dev, uint8_t terminal, void *ctx);

constexpr uac2_ops uac2_ops_table = {
    .sof_cb = UsbAudioBridge::sof_callback,
    .terminal_update_cb = UsbAudioBridge::terminal_callback,
    .get_recv_buf = UsbAudioBridge::buffer_callback,
    .data_recv_cb = UsbAudioBridge::data_callback,
    .buf_release_cb = UsbAudioBridge::release_callback,
    .feedback_cb = feedback_callback,
};

// ============================================================================
// Callback Trampolines (C -> C++)
// ============================================================================

void UsbAudioBridge::sof_callback(const device *, void *ctx) {
  static_cast<UsbAudioBridge *>(ctx)->handle_sof();
}

void UsbAudioBridge::terminal_callback(const device *, uint8_t terminal, bool enabled, bool, void *ctx) {
  static_cast<UsbAudioBridge *>(ctx)->handle_terminal_change(terminal, enabled);
}

void *UsbAudioBridge::buffer_callback(const device *, uint8_t terminal, uint16_t size, void *ctx) {
  return static_cast<UsbAudioBridge *>(ctx)->handle_buffer_request(terminal, size);
}

void UsbAudioBridge::data_callback(const device *, uint8_t terminal, void *buf, uint16_t size, void *ctx) {
  static_cast<UsbAudioBridge *>(ctx)->handle_usb_data(terminal, buf, size);
}

void UsbAudioBridge::release_callback(const device *, uint8_t, void *, void *) {
  // Static pools, nothing to release
}

static uint32_t feedback_callback(const device *dev, uint8_t terminal, void *ctx) {
  // nur für Playback/OUT-Terminal Feedback liefern
  if (terminal != UsbTerminals::OUT) {
    return 0;
  }

  // Full-Speed: Q10.14, Samples pro Frame (1 ms)
  // 8kHz => 8 samples/frame
  return (8u << 14); // 0x020000
}

size_t UsbAudioBridge::sa818_tx_callback(const device *, uint8_t *buf, size_t size, void *ctx) {
  auto &bridge = *static_cast<UsbAudioBridge *>(ctx);
  if (!bridge.state_.tx_enabled)
    return 0;

  MutexLock lock(bridge.mutex_);
  return bridge.tx_ring_.read(buf, size);
}

void UsbAudioBridge::sa818_rx_callback(const device *, const uint8_t *buf, size_t size, void *ctx) {
  auto &bridge = *static_cast<UsbAudioBridge *>(ctx);
  if (!bridge.state_.rx_enabled)
    return;

  MutexLock lock(bridge.mutex_);
  const auto written = bridge.rx_ring_.write(buf, size);
  if (written < size) {
    LOG_WRN("RX overflow: %zu/%zu dropped", size - written, size);
  }
}

void UsbAudioBridge::thread_entry(void *p1, void *, void *) {
  static_cast<UsbAudioBridge *>(p1)->handle_usb_in_thread();
}

// ============================================================================
// Event Handlers
// ============================================================================

void UsbAudioBridge::handle_sof() {
  // Inject zero-fill if no data received this SOF
  if (!state_.usb_data_received && state_.any_enabled()) {
    void *buf = nullptr;
    {
      MutexLock lock(mutex_);
      buf = usb_out_pool_.acquire();
    } // lock is automatically released here before recursive call
    handle_usb_data(UsbTerminals::OUT, buf, 0);
  }
  state_.usb_data_received = false;

  // Start streaming after TX delay
  if (!state_.streaming_active && state_.any_enabled() && state_.sof_counter >= SyncConfig::TX_START_DELAY) {
    state_.streaming_active = true;
    state_.sof_counter = 0;
    LOG_INF("Streaming started (TX delay passed)");
  }

  // Signal RX thread when ready
  if (state_.streaming_active && state_.rx_enabled) {
    if (state_.sof_counter < 255)
      state_.sof_counter++;
    if (state_.sof_counter >= SyncConfig::RX_START_DELAY) {
      k_sem_give(&usb_in_sem_);
    }
  }
}

void UsbAudioBridge::handle_terminal_change(uint8_t terminal, bool enabled) {
  MutexLock lock(mutex_);

  if (terminal == UsbTerminals::OUT) {
    state_.tx_enabled = enabled;
    LOG_INF("USB OUT (PC->SA818): %s", enabled ? "ENABLED" : "DISABLED");
    if (!enabled)
      tx_ring_.clear();
  } else if (terminal == UsbTerminals::IN) {
    state_.rx_enabled = enabled;
    LOG_INF("USB IN (SA818->PC): %s", enabled ? "ENABLED" : "DISABLED");
    if (!enabled)
      rx_ring_.clear();
  }

  // Reset state when all terminals disabled
  if (!state_.any_enabled() && state_.streaming_active) {
    state_.reset();
    feedback_.reset();
    LOG_INF("All terminals disabled, reset state");
  }
}

void *UsbAudioBridge::handle_buffer_request(uint8_t terminal, uint16_t size) {
  if (terminal != UsbTerminals::OUT || !state_.tx_enabled) {
    return nullptr;
  }

  if (size > BufferConfig::USB_BUF_SIZE) {
    LOG_ERR("Buffer request too large: %u > %zu", size, BufferConfig::USB_BUF_SIZE);
    return nullptr;
  }

  // First attempt: try to acquire from pool
  MutexLock lock(mutex_);
  void *p = usb_out_pool_.acquire();

  if (p != nullptr) {
    return p;
  }

  // Pool exhausted - log diagnostics
  const auto &stats = usb_out_pool_.stats();
  LOG_ERR("USB OUT buffer pool exhausted!");
  LOG_ERR("  Acquired: %u | Released: %u | In use: %u/%zu", stats.acquired, stats.released, stats.current_usage, BufferConfig::USB_POOL_COUNT);
  LOG_ERR("  Peak usage: %u | Exhaustion count: %u", stats.peak_usage, stats.pool_exhausted);

  // Emergency fallback: return dedicated emergency buffer
  // Note: This is NOT thread-safe if multiple endpoints request simultaneously!
  // Only used to prevent USB stack from crashing.
  static uint8_t emergency_buffer[BufferConfig::USB_BUF_SIZE] __aligned(4);
  LOG_WRN("Using emergency buffer - potential data corruption risk!");

  return emergency_buffer;
}
/*
void *UsbAudioBridge::handle_buffer_request(uint8_t terminal, uint16_t size) {
  if (terminal != UsbTerminals::OUT || !state_.tx_enabled) {
    return nullptr;
  }

  if (size > BufferConfig::USB_BUF_SIZE) {
    LOG_ERR("Buffer request too large: %u > %zu", size, BufferConfig::USB_BUF_SIZE);
    return nullptr;
  }

  MutexLock lock(mutex_);
  return usb_out_pool_.acquire();
}*/

void UsbAudioBridge::handle_usb_data(uint8_t terminal, void *buf, uint16_t size) {
  if (terminal != UsbTerminals::OUT)
    return;

  state_.usb_data_received = true;

  if (!state_.any_enabled())
    return;

  // Zero-fill based on last feedback pattern
  if (size == 0) {
    const auto pattern = feedback_.last_pattern();
    if (pattern == 1) {
      size = (AudioConfig::USB_SAMPLES_PER_SOF + 1) * AudioConfig::BYTES_PER_SAMPLE;
    } else if (pattern == 2) {
      size = (AudioConfig::USB_SAMPLES_PER_SOF - 1) * AudioConfig::BYTES_PER_SAMPLE;
    } else {
      size = AudioConfig::USB_BYTES_PER_SOF;
    }
    memset(buf, 0, size);
  }

  MutexLock lock(mutex_);
  const auto written = tx_ring_.write(static_cast<uint8_t *>(buf), size);

  if (written < size) {
    LOG_WRN("TX overflow: %u/%u bytes dropped", size - written, size);
  }

  if (written > 0) {
    state_.sof_counter++;
  }

  LOG_DBG("USB OUT: %u bytes written", written);
}

void UsbAudioBridge::handle_usb_in_thread() {
  while (true) {
    k_sem_take(&usb_in_sem_, K_MSEC(2));

    int samples = 0;
    uint16_t bytes = 0;
    void *buf = nullptr;
    size_t read = 0;
    bool should_continue = false;

    {
      MutexLock lock(mutex_);

      if (!state_.rx_enabled || !state_.streaming_active) {
        should_continue = true;
      } else {
        samples = feedback_.calculate_samples();
        bytes = samples * AudioConfig::BYTES_PER_SAMPLE;

        if (rx_ring_.available() < bytes) {
          LOG_DBG("Not enough RX data: %u < %u", rx_ring_.available(), bytes);
          should_continue = true;
        } else {
          buf = usb_in_pool_.acquire();
          read = rx_ring_.read(static_cast<uint8_t *>(buf), bytes);
        }
      }
    } // mutex_ unlocked here

    if (should_continue) {
      continue;
    }

    if (read == bytes && buf != nullptr) {
      const int ret = usbd_uac2_send(uac2_dev_, UsbTerminals::IN, buf, read);
      if (ret == 0) {
        LOG_DBG("USB IN: %u bytes sent (%d samples)", read, samples);
      } else {
        LOG_WRN("USB IN failed: %d", ret);
      }
    }
  }
}

// ============================================================================
// Initialization
// ============================================================================

int UsbAudioBridge::initialize(const device *sa818, const device *uac2) {
  if (sa818_dev_ != nullptr) {
    LOG_WRN("Already initialized");
    return 0;
  }

  sa818_dev_ = sa818;
  uac2_dev_ = uac2;

  // Initialize buffers
  tx_ring_.initialize();
  rx_ring_.initialize();
  usb_out_pool_.reset();
  usb_in_pool_.reset();

  // Initialize sync primitives
  k_mutex_init(&mutex_);
  k_sem_init(&usb_in_sem_, 0, 1);

  // Reset state
  state_ = StreamState{};
  feedback_.reset();

  // Register UAC2 callbacks
  usbd_uac2_set_ops(uac2, &uac2_ops_table, this);
  LOG_INF("UAC2 callbacks registered");

  // Register SA818 callbacks
  const sa818_audio_callbacks sa818_cbs = {
      .tx_request = sa818_tx_callback,
      .rx_data = sa818_rx_callback,
      .user_data = this,
  };

  if (sa818_audio_stream_register(sa818, &sa818_cbs) != SA818_OK) {
    LOG_ERR("SA818 callback registration failed");
    return -EIO;
  }

  // Start SA818 streaming
  const sa818_audio_format fmt = {
      .sample_rate = AudioConfig::SAMPLE_RATE_HZ,
      .bit_depth = 16,
      .channels = 1,
  };

  if (sa818_audio_stream_start(sa818, &fmt) != SA818_OK) {
    LOG_ERR("SA818 stream start failed");
    return -EIO;
  }

  LOG_INF("Bridge initialized: 8kHz 16-bit mono");
  LOG_INF("  TX buffer: %zu bytes | RX buffer: %zu bytes", BufferConfig::TX_RING_SIZE, BufferConfig::RX_RING_SIZE);

  // Create USB IN thread (for SA818 RX -> USB IN)
  k_thread_create(&thread_data_, usb_in_thread_stack, K_KERNEL_STACK_SIZEOF(usb_in_thread_stack), thread_entry, this, nullptr, nullptr, 7, 0, K_NO_WAIT);

  LOG_INF("USB IN thread started");

  return 0;
}

} // namespace oe5xrx::audio
