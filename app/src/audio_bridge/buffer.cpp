/**
 * @file buffer.cpp
 * @brief Template implementations for ring buffers and buffer pools
 *
 * @copyright Copyright (c) 2026 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#include "buffer.h"

#include "config.h"

namespace oe5xrx::audio {

// ============================================================================
// StaticRingBuffer Implementation
// ============================================================================

template <size_t Size> void StaticRingBuffer<Size>::initialize() {
  ring_buf_init(&ring_, Size, buffer_);
}

template <size_t Size> uint32_t StaticRingBuffer<Size>::write(const uint8_t *data, size_t len) {
  return ring_buf_put(&ring_, data, len);
}

template <size_t Size> uint32_t StaticRingBuffer<Size>::read(uint8_t *data, size_t len) {
  return ring_buf_get(&ring_, data, len);
}

template <size_t Size> uint32_t StaticRingBuffer<Size>::available() const {
  return ring_buf_size_get(&ring_);
}

template <size_t Size> void StaticRingBuffer<Size>::clear() {
  ring_buf_reset(&ring_);
}

// ============================================================================
// BufferPool Implementation
// ============================================================================

template <size_t Count, size_t BufSize> void *BufferPool<Count, BufSize>::acquire() {
  for (size_t i = 0; i < Count; i++) {
    if (available_[i]) {
      available_[i] = false;
      stats_.acquired++;
      stats_.current_usage++;
      if (stats_.current_usage > stats_.peak_usage) {
        stats_.peak_usage = stats_.current_usage;
      }
      return &pool_[i][0];
    }
  }
  stats_.pool_exhausted++;
  return nullptr;
}

template <size_t Count, size_t BufSize> void BufferPool<Count, BufSize>::release(void *ptr) {
  if (!ptr) {
    return;
  }

  // Find buffer index
  const auto *buf_ptr = static_cast<const uint8_t *>(ptr);
  for (size_t i = 0; i < Count; i++) {
    if (buf_ptr == &pool_[i][0]) {
      if (!available_[i]) {
        available_[i] = true;
        stats_.released++;
        if (stats_.current_usage > 0) {
          stats_.current_usage--;
        }
      }
      return;
    }
  }
}

template <size_t Count, size_t BufSize> void BufferPool<Count, BufSize>::reset() {
  for (size_t i = 0; i < Count; i++) {
    available_[i] = true;
  }
  stats_ = Stats{};
}

template <size_t Count, size_t BufSize> const typename BufferPool<Count, BufSize>::Stats &BufferPool<Count, BufSize>::stats() const noexcept {
  return stats_;
}

template <size_t Count, size_t BufSize> size_t BufferPool<Count, BufSize>::available_count() const noexcept {
  size_t count = 0;
  for (size_t i = 0; i < Count; i++) {
    if (available_[i]) {
      count++;
    }
  }
  return count;
}

// ============================================================================
// Explicit Template Instantiations
// ============================================================================

// StaticRingBuffer instantiations (from BufferConfig)
// Note: TX_RING_SIZE and RX_RING_SIZE are both 512, so only one instantiation needed
template class StaticRingBuffer<512>;

// BufferPool instantiations (from BufferConfig)
template class BufferPool<8, 32>; // 8 buffers, 32 bytes each

} // namespace oe5xrx::audio
