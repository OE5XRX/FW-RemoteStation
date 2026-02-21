/**
 * @file config.h
 * @brief Compile-time configuration for USB Audio Bridge
 *
 * @copyright Copyright (c) 2026 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>

namespace oe5xrx::audio {

/**
 * @brief Audio stream configuration
 *
 * C++20: All values are constexpr for compile-time evaluation
 */
struct AudioConfig {
  static constexpr uint32_t SAMPLE_RATE_HZ = 8000;
  static constexpr uint32_t SAMPLE_SIZE_BYTES = 2; // 16-bit
  static constexpr uint32_t CHANNELS = 1;          // Mono
  static constexpr uint32_t BYTES_PER_SAMPLE = SAMPLE_SIZE_BYTES * CHANNELS;

  static constexpr uint32_t USB_SAMPLES_PER_SOF = 8;
  static constexpr uint32_t USB_BYTES_PER_SOF = USB_SAMPLES_PER_SOF * BYTES_PER_SAMPLE;
};

/**
 * @brief Buffer sizes and pool configuration
 *
 * C++20: Compile-time validation with static_assert
 */
struct BufferConfig {
  static constexpr size_t TX_RING_SIZE = 512; // 32ms buffer (USB->SA818)
  static constexpr size_t RX_RING_SIZE = 512; // 32ms buffer (SA818->USB)
  static constexpr size_t USB_POOL_COUNT = 8;
  static constexpr size_t USB_BUF_SIZE = 32;

  // Validate power of 2 for ring buffers
  static_assert((TX_RING_SIZE & (TX_RING_SIZE - 1)) == 0, "TX_RING_SIZE must be power of 2");
  static_assert((RX_RING_SIZE & (RX_RING_SIZE - 1)) == 0, "RX_RING_SIZE must be power of 2");

  // Calculate buffer duration in milliseconds at compile time
  static constexpr uint32_t TX_BUFFER_DURATION_MS = (TX_RING_SIZE * 1000u) / (AudioConfig::SAMPLE_RATE_HZ * AudioConfig::BYTES_PER_SAMPLE);
  static constexpr uint32_t RX_BUFFER_DURATION_MS = (RX_RING_SIZE * 1000u) / (AudioConfig::SAMPLE_RATE_HZ * AudioConfig::BYTES_PER_SAMPLE);
};

/**
 * @brief USB terminal IDs (from device tree)
 */
struct UsbTerminals {
  static constexpr uint8_t OUT = 1; // USB -> SA818 TX
  static constexpr uint8_t IN = 4;  // SA818 RX -> USB
};

/**
 * @brief Synchronization configuration
 */
struct SyncConfig {
  static constexpr uint8_t TX_START_DELAY = 2; // SOF frames before TX starts
  static constexpr uint8_t RX_START_DELAY = 3; // Additional SOF frames before RX starts
};

} // namespace oe5xrx::audio
