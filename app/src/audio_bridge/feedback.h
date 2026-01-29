/**
 * @file feedback.h
 * @brief Implicit feedback controller for USB audio synchronization
 *
 * @copyright Copyright (c) 2026 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#pragma once

#include "config.h"

#include <stdint.h>

namespace oe5xrx::audio {

/**
 * @brief Implicit feedback controller
 *
 * Dynamically adjusts sample count (7/8/9 samples per SOF) based on
 * buffer fill levels to maintain synchronization between USB and SA818.
 *
 * Uses rolling history to prevent oscillation and ensure smooth adaptation.
 */
class ImplicitFeedbackController {
public:
  /**
   * @brief Calculate number of samples for next SOF
   * @return Sample count (typically 7, 8, or 9)
   */
  int calculate_samples() {
    constexpr int nominal = AudioConfig::USB_SAMPLES_PER_SOF;

    // Update rolling history (keep last 8 adjustments)
    plus_ones_ = (plus_ones_ << 1) & 0xFF;
    minus_ones_ = (minus_ones_ << 1) & 0xFF;

    // TODO: Measure actual offset from buffer levels
    // For now, run at nominal rate
    constexpr int offset = 0;

    // Add extra sample if running slow
    if (offset < 0 && popcount(plus_ones_) < -offset) {
      plus_ones_ |= 1;
      return nominal + 1;
    }

    // Remove sample if running fast
    if (offset > 0 && popcount(minus_ones_) < offset) {
      minus_ones_ |= 1;
      return nominal - 1;
    }

    return nominal;
  }

  /**
   * @brief Reset feedback state
   */
  void reset() {
    plus_ones_ = 0;
    minus_ones_ = 0;
  }

  /**
   * @brief Get last adjustment pattern
   * @return 0=nominal, 1=+1, 2=-1
   */
  uint32_t last_pattern() const {
    if (plus_ones_ & 1)
      return 1; // +1 pattern
    if (minus_ones_ & 1)
      return 2; // -1 pattern
    return 0;   // Nominal
  }

private:
  static int popcount(uint32_t x) { return __builtin_popcount(x); }

  uint32_t plus_ones_{0};  // Rolling history of +1 adjustments
  uint32_t minus_ones_{0}; // Rolling history of -1 adjustments
};

} // namespace oe5xrx::audio
