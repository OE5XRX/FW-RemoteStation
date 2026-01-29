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
  void initialize() { ring_buf_init(&ring_, Size, buffer_); }

  uint32_t write(const uint8_t *data, size_t len) { return ring_buf_put(&ring_, data, len); }

  uint32_t read(uint8_t *data, size_t len) { return ring_buf_get(&ring_, data, len); }

  uint32_t available() const { return ring_buf_size_get(&ring_); }

  void clear() { ring_buf_reset(&ring_); }

private:
  alignas(UDC_BUF_ALIGN) uint8_t buffer_[Size]{};
  ring_buf ring_{};
};

/**
 * @brief Static buffer pool for USB transfers
 *
 * Pre-allocated pool of fixed-size buffers for zero-allocation USB operations.
 *
 * @tparam Count Number of buffers in pool
 * @tparam BufSize Size of each buffer
 */
template <size_t Count, size_t BufSize> class BufferPool {
public:
  void *acquire() {
    const auto idx = index_;
    index_ = (index_ + 1) % Count;
    return pool_[idx];
  }

  void reset() { index_ = 0; }

private:
  alignas(UDC_BUF_ALIGN) uint8_t pool_[Count][BufSize]{};
  uint8_t index_{0};
};

} // namespace oe5xrx::audio
