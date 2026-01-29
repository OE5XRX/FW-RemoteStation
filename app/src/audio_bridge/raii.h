/**
 * @file raii.h
 * @brief RAII wrappers for Zephyr primitives
 *
 * @copyright Copyright (c) 2026 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#pragma once

#include <zephyr/kernel.h>

namespace oe5xrx::audio {

/**
 * @brief RAII wrapper for k_mutex
 *
 * Automatically locks mutex on construction and unlocks on destruction.
 * Prevents forgotten unlocks and ensures exception safety.
 */
class MutexLock {
public:
  explicit MutexLock(k_mutex &mutex) : mutex_(mutex) { k_mutex_lock(&mutex_, K_FOREVER); }

  ~MutexLock() { k_mutex_unlock(&mutex_); }

  // Non-copyable, non-movable
  MutexLock(const MutexLock &) = delete;
  MutexLock &operator=(const MutexLock &) = delete;
  MutexLock(MutexLock &&) = delete;
  MutexLock &operator=(MutexLock &&) = delete;

private:
  k_mutex &mutex_;
};

} // namespace oe5xrx::audio
