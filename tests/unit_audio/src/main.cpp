/*
 * Copyright (c) 2025 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Unit tests for the UAC2 explicit-feedback regulator (native_sim).
 */
#include "feedback.h"

#include <zephyr/ztest.h>

/* fm_board audio: 8 kHz, 16-bit mono => 8 samples/SOF, TX ring 256 samples. */
static constexpr uint16_t kSamplesPerSof = 8;
static constexpr size_t kCapacity = 256; /* samples */
static constexpr uint32_t kNominal = static_cast<uint32_t>(kSamplesPerSof) << 14;

ZTEST_SUITE(feedback, NULL, NULL, NULL, NULL, NULL);

ZTEST(feedback, test_nominal_at_setpoint) {
  usb_audio::BufferFeedback fb;
  fb.init(kSamplesPerSof);
  fb.update(kCapacity / 2, kCapacity); /* error 0 */
  zassert_equal(fb.value(), kNominal, "expected nominal %u, got %u", (unsigned)kNominal, (unsigned)fb.value());
}

ZTEST(feedback, test_too_full_lowers_feedback) {
  usb_audio::BufferFeedback fb;
  fb.init(kSamplesPerSof);
  fb.update(160, kCapacity); /* fill > set point */
  zassert_true(fb.value() < kNominal, "value %u should be below nominal %u", (unsigned)fb.value(), (unsigned)kNominal);
}

ZTEST(feedback, test_too_empty_raises_feedback) {
  usb_audio::BufferFeedback fb;
  fb.init(kSamplesPerSof);
  fb.update(96, kCapacity); /* fill < set point */
  zassert_true(fb.value() > kNominal, "value %u should be above nominal %u", (unsigned)fb.value(), (unsigned)kNominal);
}

ZTEST(feedback, test_clamp_upper) {
  usb_audio::BufferFeedback fb;
  fb.init(kSamplesPerSof);
  fb.update(0, kCapacity); /* maximally empty => +0.5 sample clamp */
  zassert_equal(fb.value(), kNominal + (1u << 13), "upper clamp: got %u", (unsigned)fb.value());
}

ZTEST(feedback, test_clamp_lower) {
  usb_audio::BufferFeedback fb;
  fb.init(kSamplesPerSof);
  fb.update(kCapacity, kCapacity); /* maximally full => -0.5 sample clamp */
  zassert_equal(fb.value(), kNominal - (1u << 13), "lower clamp: got %u", (unsigned)fb.value());
}

ZTEST(feedback, test_converges_under_drift) {
  usb_audio::BufferFeedback fb;
  fb.init(kSamplesPerSof);

  const int32_t cap = (int32_t)kCapacity;
  const int32_t setp = cap / 2;
  /* Ring fill in Q14 sample units. Sink consumes nominal + 66 LSB, i.e.
   * ~0.004 sample/frame == the USB 500 ppm worst-case drift. */
  int64_t ring_q14 = (int64_t)setp << 14;
  const int64_t consume_q14 = ((int64_t)kSamplesPerSof << 14) + 66;

  for (int i = 0; i < 60000; i++) { /* 60 s at 1 kHz SOF */
    int32_t used = (int32_t)(ring_q14 >> 14);
    if (used < 0)
      used = 0;
    if (used > cap)
      used = cap;
    fb.update((size_t)used, (size_t)cap);
    /* Masking invariant: reported value must always have its low 4 bits clear.
     * As the integrator ramps, integrator_/kTi takes many non-16-aligned values,
     * so without the mask this would fail at some iteration. */
    zassert_equal(fb.value() % 16, 0U, "value %u not 16-aligned at frame %d", (unsigned)fb.value(), i);
    ring_q14 += (int64_t)fb.value(); /* host delivers the reported average */
    ring_q14 -= consume_q14;
    zassert_true(ring_q14 > 0, "ring underran at frame %d", i);
    zassert_true((ring_q14 >> 14) < cap, "ring overran at frame %d", i);
  }
  int32_t final_used = (int32_t)(ring_q14 >> 14);
  zassert_true(final_used > setp - 50 && final_used < setp + 50, "ring not centered near set point: %d", final_used);
}

ZTEST(feedback, test_integrator_is_bounded) {
  usb_audio::BufferFeedback fb;
  fb.init(kSamplesPerSof);

  /* Sustained maximal empty-ring error drives the integrator to its bound.
   * Saturation needs integ_limit_/128 = 131072 steps; run well past that. */
  for (int i = 0; i < 500000; i++) {
    fb.update(0, kCapacity);
  }
  zassert_equal(fb.value(), kNominal + (1u << 13), "saturated output must sit at the upper clamp, got %u", (unsigned)fb.value());

  /* Reverse to sustained maximal full-ring error. With a BOUNDED integrator
   * (capped at 16,777,216) the output reaches the lower clamp in ~131k steps;
   * an unbounded integrator wound to 500000*128 = 64,000,000 would need ~500k.
   * The 200000 threshold sits between the two and fails iff unbounded. */
  int steps = 0;
  while (fb.value() != kNominal - (1u << 13) && steps < 500000) {
    fb.update(kCapacity, kCapacity);
    steps++;
  }
  zassert_true(steps < 200000, "integrator not bounded: recovery took %d steps", steps);
  zassert_equal(fb.value(), kNominal - (1u << 13), "did not recover to the lower clamp, got %u", (unsigned)fb.value());
}
