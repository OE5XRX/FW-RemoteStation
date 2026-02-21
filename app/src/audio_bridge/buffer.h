/**
 * @file buffer.h
 * @brief Static ring buffers and buffer pools
 *
 * @copyright Copyright (c) 2026 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/usb/usbd.h>

namespace oe5xrx::audio {

/**
 * @brief Type-safe ring buffer wrapper with static allocation
 *
 * @tparam Size Buffer size in bytes (must be power of 2)
 */
template <size_t Size> class StaticRingBuffer {
public:
  void initialize();
  uint32_t write(const uint8_t *data, size_t len);
  uint32_t read(uint8_t *data, size_t len);
  uint32_t available() const;
  void clear();

private:
  alignas(UDC_BUF_ALIGN) uint8_t buffer_[Size]{};
  ring_buf ring_{};
};

/**
 * @brief Static buffer pool for USB transfers
 *
 * Pre-allocated pool of fixed-size buffers for zero-allocation USB operations.
 * Thread-safe with availability tracking and diagnostics.
 *
 * @tparam Count Number of buffers in pool
 * @tparam BufSize Size of each buffer
 */
template <size_t Count, size_t BufSize> class BufferPool {
public:
  /**
   * @brief Pool usage statistics (C++20 designated initializers)
   */
  struct Stats {
    uint32_t acquired{0};       ///< Total acquire calls
    uint32_t released{0};       ///< Total release calls
    uint32_t current_usage{0};  ///< Currently allocated buffers
    uint32_t peak_usage{0};     ///< Maximum simultaneous usage
    uint32_t pool_exhausted{0}; ///< Times pool was empty
  };

  /**
   * @brief Acquire buffer from pool
   * @return Pointer to buffer, or nullptr if pool exhausted
   */
  void *acquire();

  /**
   * @brief Release buffer back to pool
   * @param ptr Pointer to buffer (must be from this pool)
   */
  void release(void *ptr);

  /**
   * @brief Reset pool to initial state
   */
  void reset();

  /**
   * @brief Get pool statistics
   */
  [[nodiscard]] const Stats &stats() const noexcept;

  /**
   * @brief Get number of available buffers
   */
  [[nodiscard]] size_t available_count() const noexcept;

private:
  alignas(UDC_BUF_ALIGN) uint8_t pool_[Count][BufSize]{};
  bool available_[Count]{true}; // C++20: default init for all elements
  Stats stats_{};
};

} // namespace oe5xrx::audio
