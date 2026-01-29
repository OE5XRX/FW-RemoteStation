/**
 * @file bridge.h
 * @brief USB Audio Bridge main interface
 *
 * @copyright Copyright (c) 2026 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#pragma once

#include "buffer.h"
#include "config.h"
#include "feedback.h"

#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

namespace oe5xrx::audio {

/**
 * @brief Main USB Audio Bridge
 *
 * Connects USB UAC2 audio interface with SA818 radio transceiver.
 * Implements bidirectional audio streaming with implicit feedback synchronization.
 *
 * Thread-safe singleton with zero dynamic memory allocation.
 */
class UsbAudioBridge {
public:
  /**
   * @brief Get singleton instance
   */
  static UsbAudioBridge &instance() {
    static UsbAudioBridge bridge;
    return bridge;
  }

  /**
   * @brief Initialize bridge and start audio streaming
   *
   * @param sa818 SA818 device
   * @param uac2 USB UAC2 device
   * @return 0 on success, negative error code otherwise
   */
  int initialize(const device *sa818, const device *uac2);

  // ---- Static Callback Trampolines (C -> C++) ----
  // Must be public for uac2_ops_table and SA818 callbacks

  static void sof_callback(const device *, void *ctx);
  static void terminal_callback(const device *, uint8_t terminal, bool enabled, bool, void *ctx);
  static void *buffer_callback(const device *, uint8_t terminal, uint16_t size, void *ctx);
  static void data_callback(const device *, uint8_t terminal, void *buf, uint16_t size, void *ctx);
  static void release_callback(const device *, uint8_t, void *, void *);

  static size_t sa818_tx_callback(const device *, uint8_t *buf, size_t size, void *ctx);
  static void sa818_rx_callback(const device *, const uint8_t *buf, size_t size, void *ctx);

  static void thread_entry(void *p1, void *, void *);

private:
  UsbAudioBridge() = default;
  ~UsbAudioBridge() = default;

  // Non-copyable, non-movable
  UsbAudioBridge(const UsbAudioBridge &) = delete;
  UsbAudioBridge &operator=(const UsbAudioBridge &) = delete;

  // ---- Internal State ----

  struct StreamState {
    bool tx_enabled{false};        // USB OUT -> SA818 TX
    bool rx_enabled{false};        // SA818 RX -> USB IN
    bool streaming_active{false};  // After startup delay
    bool usb_data_received{false}; // Data received this SOF
    uint8_t sof_counter{0};        // SOF frame counter

    bool any_enabled() const { return tx_enabled || rx_enabled; }
    void reset() {
      streaming_active = false;
      sof_counter = 0;
    }
  };

  // ---- Event Handlers ----

  void handle_sof();
  void handle_terminal_change(uint8_t terminal, bool enabled);
  void *handle_buffer_request(uint8_t terminal, uint16_t size);
  void handle_usb_data(uint8_t terminal, void *buf, uint16_t size);
  void handle_usb_in_thread();

  // ---- Member Variables (All Static) ----

  const device *sa818_dev_{nullptr};
  const device *uac2_dev_{nullptr};

  StaticRingBuffer<BufferConfig::TX_RING_SIZE> tx_ring_;
  StaticRingBuffer<BufferConfig::RX_RING_SIZE> rx_ring_;

  BufferPool<BufferConfig::USB_POOL_COUNT, BufferConfig::USB_BUF_SIZE> usb_out_pool_;
  BufferPool<BufferConfig::USB_POOL_COUNT, BufferConfig::USB_BUF_SIZE> usb_in_pool_;

  k_mutex mutex_{};
  k_sem usb_in_sem_{}; // Signals USB IN thread when SA818 RX data is ready

  StreamState state_{};
  ImplicitFeedbackController feedback_;

  k_thread thread_data_{};
};

// Thread stack declaration (defined in bridge.cpp)
extern k_thread_stack_t usb_in_thread_stack[K_KERNEL_STACK_LEN(1024)];

} // namespace oe5xrx::audio
