/**
 * @file feedback.h
 * @brief Explicit-feedback regulator for the UAC2 OUT (playback) sink.
 *
 * Full-Speed only: the feedback value is Q10.14 (samples-per-SOF << 14). The
 * regulator keeps a software-timed, ring-buffered sink near half full by
 * nudging the reported samples/frame around nominal with a fixed-point PI
 * controller. Pure logic: no USB, no Zephyr, no heap, no float, no exceptions.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */
#ifndef OE5XRX_USB_AUDIO_FEEDBACK_H_
#define OE5XRX_USB_AUDIO_FEEDBACK_H_

#include <cstddef>
#include <cstdint>

namespace usb_audio {

class BufferFeedback {
public:
  /** Set nominal value + gains for @p samples_per_sof and reset the loop. */
  void init(uint16_t samples_per_sof);

  /** Reset the integrator and set the reported value back to nominal. */
  void reset();

  /**
   * Run one control step. Call once per SOF while the OUT stream is active.
   * @param used     current ring fill, in samples
   * @param capacity ring capacity, in samples (set point is capacity/2)
   */
  void update(size_t used, size_t capacity);

  /** Current Q10.14 feedback value to report to the host. */
  uint32_t value() const { return fb_value_; }

  /** Nominal Q10.14 value (no correction). */
  uint32_t nominal() const { return nominal_; }

private:
  /* Full-Speed feedback is Q10.14. */
  static constexpr int kFracBits = 14;
  /* Clear the low bits: do not use the optional extra resolution. */
  static constexpr int kLsbZeroBits = 4;
  /* Proportional divisor: a full half-ring error (capacity/2 samples) maps to
   * ~0.5 sample of correction, i.e. the clamp. Chosen so normal few-sample
   * jitter produces only a gentle nudge. */
  static constexpr int32_t kInvKp = 256;
  /* Integral time in SOFs. The integrator carries the steady drift. */
  static constexpr int32_t kTi = 2048;

  uint32_t nominal_ = 0;    /* samples_per_sof << 14 */
  uint32_t fb_value_ = 0;   /* current reported value (clamped, LSB-masked) */
  int32_t clamp_ = 0;       /* max deviation from nominal, Q10.14 (±0.5 sample) */
  int32_t integrator_ = 0;  /* bounded accumulator (anti-windup) */
  int32_t integ_limit_ = 0; /* |integrator_| bound so I alone tops out at clamp */
};

} // namespace usb_audio

#endif /* OE5XRX_USB_AUDIO_FEEDBACK_H_ */
