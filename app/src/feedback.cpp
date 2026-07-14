/**
 * @file feedback.cpp
 * @brief BufferFeedback PI regulator implementation. See feedback.h.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */
#include "feedback.h"

namespace usb_audio {

void BufferFeedback::init(uint16_t samples_per_sof) {
  nominal_ = static_cast<uint32_t>(samples_per_sof) << kFracBits;
  clamp_ = 1 << (kFracBits - 1); /* ±0.5 sample */
  integ_limit_ = clamp_ * kTi;   /* integrator alone tops out at the clamp */
  reset();
}

void BufferFeedback::reset() {
  integrator_ = 0;
  fb_value_ = nominal_;
}

void BufferFeedback::update(size_t used, size_t capacity) {
  const int32_t set_point = static_cast<int32_t>(capacity / 2);
  /* Positive error => ring emptier than target => host too slow => ask for
   * MORE samples => raise feedback. Negative => the opposite. */
  const int32_t error = set_point - static_cast<int32_t>(used);

  integrator_ += error;
  if (integrator_ > integ_limit_) {
    integrator_ = integ_limit_;
  } else if (integrator_ < -integ_limit_) {
    integrator_ = -integ_limit_;
  }

  /* P term widened to int64 and computed with a multiply (not a signed left
   * shift, which is only well-defined for negative operands from C++20 on) so
   * it stays correct and overflow-free even if the ring range grows. */
  int32_t correction = static_cast<int32_t>((static_cast<int64_t>(error) * (1 << kFracBits)) / kInvKp) + integrator_ / kTi;
  if (correction > clamp_) {
    correction = clamp_;
  } else if (correction < -clamp_) {
    correction = -clamp_;
  }

  /* nominal_ dominates and correction is small & clamped, so the sum is always
   * positive; mask off the unused low bits like the Zephyr sample does. */
  int32_t val = static_cast<int32_t>(nominal_) + correction;
  val &= ~((1 << kLsbZeroBits) - 1);
  fb_value_ = static_cast<uint32_t>(val);
}

} // namespace usb_audio
