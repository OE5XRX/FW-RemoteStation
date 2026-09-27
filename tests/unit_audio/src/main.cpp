/*
 * Copyright (c) 2025 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Unit tests for the UAC2 explicit-feedback regulator (native_sim).
 */
#include "adc_pcm.h"
#include "audio_route.h"
#include "dac_pcm.h"
#include "feedback.h"

#include <zephyr/sys/ring_buffer.h>
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

ZTEST_SUITE(adc_pcm, NULL, NULL, NULL, NULL, NULL);

ZTEST(adc_pcm, test_midpoint_is_zero) {
  /* 12-bit midpoint 2048 -> left-shifted to 32768 -> minus 32768 == 0. */
  zassert_equal(adc_to_pcm16(2048, 12), 0, "midpoint must map to 0, got %d", adc_to_pcm16(2048, 12));
}

ZTEST(adc_pcm, test_full_scale_extremes) {
  /* 12-bit: 0 -> -32768; 4095 -> +32752 (0xFFF << 4 = 0xFFF0 = 65520 - 32768). */
  zassert_equal(adc_to_pcm16(0, 12), -32768, "min: got %d", adc_to_pcm16(0, 12));
  zassert_equal(adc_to_pcm16(4095, 12), 32752, "max: got %d", adc_to_pcm16(4095, 12));
}

ZTEST(adc_pcm, test_16bit_is_identity_offset) {
  /* resolution 16: no shift; 0 -> -32768, 32768 -> 0, 65535 -> 32767. */
  zassert_equal(adc_to_pcm16(0, 16), -32768, "got %d", adc_to_pcm16(0, 16));
  zassert_equal(adc_to_pcm16(32768, 16), 0, "got %d", adc_to_pcm16(32768, 16));
  zassert_equal(adc_to_pcm16(65535, 16), 32767, "got %d", adc_to_pcm16(65535, 16));
}

ZTEST_SUITE(dac_pcm, NULL, NULL, NULL, NULL, NULL);

ZTEST(dac_pcm, test_midpoint_is_dac_midscale) {
  /* 12-bit: PCM 0 -> (0+32768)>>4 = 2048 (mid-scale). */
  zassert_equal(pcm16_to_dac(0, 12), 2048, "got %u", pcm16_to_dac(0, 12));
}

ZTEST(dac_pcm, test_full_scale_extremes) {
  /* 12-bit: -32768 -> 0; 32767 -> (65535)>>4 = 4095. */
  zassert_equal(pcm16_to_dac(-32768, 12), 0, "got %u", pcm16_to_dac(-32768, 12));
  zassert_equal(pcm16_to_dac(32767, 12), 4095, "got %u", pcm16_to_dac(32767, 12));
}

ZTEST(dac_pcm, test_roundtrip_with_adc) {
  /* adc_to_pcm16 and pcm16_to_dac are inverses up to resolution truncation:
   * a DAC code fed back as an ADC reading returns the same PCM (12-bit). */
  for (uint16_t code = 0; code < 4096; code += 337) {
    int16_t pcm = adc_to_pcm16(code, 12);
    zassert_equal(pcm16_to_dac(pcm, 12), code, "code %u -> pcm %d -> %u", code, pcm, pcm16_to_dac(pcm, 12));
  }
}

/*
 * audio_route: the pure routing decision for the test-mode UAC2 loopback
 * (CONFIG_FM_TEST_LOOPBACK). usb_audio_bridge.cpp -- which only builds against
 * the USB device stack (not native_sim) -- dispatches its OUT and SA818-RX taps
 * on exactly these classifiers, so testing them here verifies the routing logic
 * on native_sim without any USB/hardware ("simulation equals hardware").
 */
using audio_route::OutRoute;
using audio_route::Sa818RxRoute;

ZTEST_SUITE(audio_route, NULL, NULL, NULL, NULL, NULL);

ZTEST(audio_route, test_out_route_truth_table) {
  /* Loopback off: USB OUT always flows to the SA818 TX ring, regardless of the
   * USB IN terminal state. This is the production path. */
  zassert_equal(audio_route::classify_out(false, false), OutRoute::TxRing);
  zassert_equal(audio_route::classify_out(false, true), OutRoute::TxRing);

  /* Loopback on: OUT is looped into the RX ring only while USB IN is active;
   * dropped otherwise so it cannot pool into stale/overflowing audio. */
  zassert_equal(audio_route::classify_out(true, true), OutRoute::RxRingLoopback);
  zassert_equal(audio_route::classify_out(true, false), OutRoute::DropInInactive);
}

ZTEST(audio_route, test_sa818_rx_route_truth_table) {
  /* Loopback off: real SA818 capture flows to the RX ring (-> USB IN). */
  zassert_equal(audio_route::classify_sa818_rx(false), Sa818RxRoute::RxRing);
  /* Loopback on: SA818 capture is dropped so it cannot mix into the RX ring,
   * which is carrying the looped-back USB OUT audio. */
  zassert_equal(audio_route::classify_sa818_rx(true), Sa818RxRoute::DropLoopback);
}

/*
 * Integration against real ring buffers: replicate the bridge's tap dispatch
 * (classify_* -> ring_buf_put) and assert the bytes land in the right ring.
 * This is the concrete "OUT lands in rx_ring, SA818 push suppressed" check.
 */
static void route_out(struct ring_buf *tx, struct ring_buf *rx, bool loopback, bool rx_enabled, const uint8_t *data, uint32_t len) {
  switch (audio_route::classify_out(loopback, rx_enabled)) {
  case OutRoute::TxRing:
    ring_buf_put(tx, data, len);
    break;
  case OutRoute::RxRingLoopback:
    ring_buf_put(rx, data, len);
    break;
  case OutRoute::DropInInactive:
    break;
  }
}

static void route_sa818_rx(struct ring_buf *rx, bool loopback, const uint8_t *data, uint32_t len) {
  if (audio_route::classify_sa818_rx(loopback) == Sa818RxRoute::RxRing) {
    ring_buf_put(rx, data, len);
  }
}

ZTEST(audio_route, test_ring_dispatch_loopback_on) {
  uint8_t tx_storage[64];
  uint8_t rx_storage[64];
  struct ring_buf tx, rx;
  ring_buf_init(&tx, sizeof(tx_storage), tx_storage);
  ring_buf_init(&rx, sizeof(rx_storage), rx_storage);

  const uint8_t usb_out[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  const uint8_t sa818[4] = {9, 9, 9, 9};

  /* Loopback armed, USB IN active: USB OUT must land in RX (not TX), and the
   * SA818 capture must be suppressed so it cannot mix in. */
  route_out(&tx, &rx, /*loopback=*/true, /*rx_enabled=*/true, usb_out, sizeof(usb_out));
  route_sa818_rx(&rx, /*loopback=*/true, sa818, sizeof(sa818));

  zassert_equal(ring_buf_size_get(&tx), 0, "TX ring must stay empty in loopback");
  zassert_equal(ring_buf_size_get(&rx), sizeof(usb_out), "RX ring must hold only the looped USB OUT bytes");

  uint8_t out[16];
  uint32_t n = ring_buf_get(&rx, out, sizeof(out));
  zassert_equal(n, sizeof(usb_out));
  zassert_mem_equal(out, usb_out, sizeof(usb_out), "looped bytes must be the USB OUT payload, not SA818 capture");
}

ZTEST(audio_route, test_ring_dispatch_loopback_off) {
  uint8_t tx_storage[64];
  uint8_t rx_storage[64];
  struct ring_buf tx, rx;
  ring_buf_init(&tx, sizeof(tx_storage), tx_storage);
  ring_buf_init(&rx, sizeof(rx_storage), rx_storage);

  const uint8_t usb_out[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  const uint8_t sa818[4] = {9, 9, 9, 9};

  /* Production path: USB OUT -> TX ring, SA818 capture -> RX ring. */
  route_out(&tx, &rx, /*loopback=*/false, /*rx_enabled=*/true, usb_out, sizeof(usb_out));
  route_sa818_rx(&rx, /*loopback=*/false, sa818, sizeof(sa818));

  zassert_equal(ring_buf_size_get(&tx), sizeof(usb_out), "USB OUT must reach the SA818 TX ring");
  zassert_equal(ring_buf_size_get(&rx), sizeof(sa818), "SA818 capture must reach the RX ring");
}

ZTEST(audio_route, test_loopback_drops_out_while_in_inactive) {
  uint8_t tx_storage[64];
  uint8_t rx_storage[64];
  struct ring_buf tx, rx;
  ring_buf_init(&tx, sizeof(tx_storage), tx_storage);
  ring_buf_init(&rx, sizeof(rx_storage), rx_storage);

  const uint8_t usb_out[8] = {1, 2, 3, 4, 5, 6, 7, 8};

  /* Loopback armed but USB IN not yet open: OUT is dropped (neither ring). */
  route_out(&tx, &rx, /*loopback=*/true, /*rx_enabled=*/false, usb_out, sizeof(usb_out));

  zassert_equal(ring_buf_size_get(&tx), 0, "no TX buffering in loopback");
  zassert_equal(ring_buf_size_get(&rx), 0, "OUT must be dropped while USB IN is inactive");
}
