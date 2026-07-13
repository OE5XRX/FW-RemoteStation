# UAC2 Explicit Feedback + Separate RX/TX Streams Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the UAC2 audio bridge into two independent asynchronous streams — a capture IN stream that the host presents as a working microphone, and a playback OUT stream governed by an explicit-feedback endpoint driven by a software TX-ring-level PI regulator.

**Architecture:** Remove the `implicit-feedback` flag from both UAC2 streaming interfaces in the fm_board devicetree. The OUT (host→SA818 TX) interface then gets a class-generated explicit feedback endpoint, which we service with a new `usb_audio::BufferFeedback` PI regulator that keeps the TX ring near half full. The IN (SA818 RX→host) interface becomes a plain asynchronous capture endpoint sent one packet per SOF. The regulator is a pure-logic C++ unit, unit-tested on `native_sim`; the bridge/DT changes are compile-verified for `fm_board` and validated end-to-end on hardware.

**Tech Stack:** Zephyr RTOS, `USB_DEVICE_STACK_NEXT` UAC2 class (`usbd_uac2`), C++20 (app layer), ztest (native_sim), Twister/pytest, devicetree.

## Global Constraints

Copied verbatim from `CLAUDE.md` / the design spec — every task implicitly includes these:

- **No dynamic memory:** no `new`/`delete`/`malloc`/`std::vector`/`std::string`/`std::function`. Use `std::array`, fixed buffers, value types.
- **No exceptions, no RTTI.** Error handling via return/status types only.
- **No floating point in the regulator.** Fixed-point integer math only.
- **clang-format-18** over all `.c/.h/.cpp/.hpp` under `app/`, `boards/`, `tests/`; the `clang_format` CI job fails on any diff. Format before every commit.
- **Full-Speed only.** `fm_board` declares `full-speed;` (no `high-speed;`), so only the Q10.14 FS feedback format is implemented. Nominal feedback = `8 << 14` (8 samples/SOF @ 8 kHz).
- **Feedback format:** Q10.14 stored in the 24 least-significant bits; upper 8 bits ignored by the class.
- **Keep firmware thin:** no persistence, no access/role model, no platform config.
- **Real test dirs:** `tests/etl`, `tests/sim_shell`, `tests/usb_audio`, and the new `tests/unit_audio` (already referenced in a commented CI step). Do not invent others.
- **Terminal IDs (fm_board topology):** `aclk=1, usb_out=2, sa818_tx=3, sa818_rx=4, usb_in=5`. `USB_OUT_TERMINAL_ID == 2`, `USB_IN_TERMINAL_ID == 5`. Removing `implicit-feedback` does not change entity IDs.

### Environment note (build reach)

`native_sim` builds do **not** compile `usb_audio_bridge.cpp` (the app uses `sim_audio` there); the bridge is compiled only for `fm_board` via `app/CMakeLists.txt` and `tests/usb_audio`. Therefore:

- Task 1 (regulator) is fully testable locally on `native_sim`.
- Tasks 2–4 (bridge + DT) are verified with a **`fm_board` build-only** Twister run. If the local environment lacks the ARM toolchain, run `west twister -T tests/usb_audio -p fm_board/stm32u575xx --build-only` where the toolchain is available (CI provides it via `action-zephyr-setup`); otherwise rely on the CI gate and do the runtime checks on hardware.

---

## File Structure

**Create:**
- `app/src/feedback.h` — `usb_audio::BufferFeedback` declaration (Q10.14 PI regulator; no USB/Zephyr deps).
- `app/src/feedback.cpp` — regulator implementation.
- `tests/unit_audio/CMakeLists.txt` — native_sim ztest project; compiles `app/src/feedback.cpp` + the test.
- `tests/unit_audio/prj.conf` — ztest + C++20 config.
- `tests/unit_audio/testcase.yaml` — native_sim ztest scenario.
- `tests/unit_audio/src/main.cpp` — `BufferFeedback` unit tests.

**Modify:**
- `boards/oe5xrx/fm_board/fm_board.dts` — remove the two `implicit-feedback` flags.
- `app/src/usb_audio_bridge.cpp` — feedback member + `feedback_cb`, SOF-driven regulator update, startup prebuffer, SOF-driven IN send, remove polling thread.
- `app/CMakeLists.txt` — add `feedback.cpp` to the `CONFIG_USB_DEVICE_STACK_NEXT` sources.
- `tests/usb_audio/CMakeLists.txt` — add `feedback.cpp` (the bridge now references it).
- `.github/workflows/ci.yml` — enable the `tests/unit_audio` native_sim Twister step.
- `app/USB_AUDIO_BRIDGE.md` — document the async / explicit-feedback topology.

---

## Task 1: `BufferFeedback` PI regulator + native_sim unit tests

**Files:**
- Create: `app/src/feedback.h`
- Create: `app/src/feedback.cpp`
- Create: `tests/unit_audio/CMakeLists.txt`
- Create: `tests/unit_audio/prj.conf`
- Create: `tests/unit_audio/testcase.yaml`
- Create: `tests/unit_audio/src/main.cpp`
- Modify: `.github/workflows/ci.yml` (enable the commented `tests/unit_audio` step)

**Interfaces:**
- Produces (consumed by Task 2):
  - `namespace usb_audio { class BufferFeedback; }`
  - `void BufferFeedback::init(uint16_t samples_per_sof);`
  - `void BufferFeedback::reset();`
  - `void BufferFeedback::update(size_t used, size_t capacity);` — `used`/`capacity` in **samples**; set point is `capacity/2`.
  - `uint32_t BufferFeedback::value() const;` — current Q10.14 feedback value.
  - `uint32_t BufferFeedback::nominal() const;` — nominal Q10.14 value.

- [ ] **Step 1: Write the header**

Create `app/src/feedback.h`:

```cpp
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
```

- [ ] **Step 2: Write the implementation**

Create `app/src/feedback.cpp`:

```cpp
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

  int32_t correction = (error << kFracBits) / kInvKp + integrator_ / kTi;
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
```

- [ ] **Step 3: Write the failing tests**

Create `tests/unit_audio/src/main.cpp`:

```cpp
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
  zassert_equal(fb.value(), kNominal, "expected nominal %u, got %u",
                (unsigned)kNominal, (unsigned)fb.value());
}

ZTEST(feedback, test_too_full_lowers_feedback) {
  usb_audio::BufferFeedback fb;
  fb.init(kSamplesPerSof);
  fb.update(160, kCapacity); /* fill > set point */
  zassert_true(fb.value() < kNominal, "value %u should be below nominal %u",
               (unsigned)fb.value(), (unsigned)kNominal);
}

ZTEST(feedback, test_too_empty_raises_feedback) {
  usb_audio::BufferFeedback fb;
  fb.init(kSamplesPerSof);
  fb.update(96, kCapacity); /* fill < set point */
  zassert_true(fb.value() > kNominal, "value %u should be above nominal %u",
               (unsigned)fb.value(), (unsigned)kNominal);
}

ZTEST(feedback, test_clamp_upper) {
  usb_audio::BufferFeedback fb;
  fb.init(kSamplesPerSof);
  fb.update(0, kCapacity); /* maximally empty => +0.5 sample clamp */
  zassert_equal(fb.value(), kNominal + (1u << 13), "upper clamp: got %u",
                (unsigned)fb.value());
}

ZTEST(feedback, test_clamp_lower) {
  usb_audio::BufferFeedback fb;
  fb.init(kSamplesPerSof);
  fb.update(kCapacity, kCapacity); /* maximally full => -0.5 sample clamp */
  zassert_equal(fb.value(), kNominal - (1u << 13), "lower clamp: got %u",
                (unsigned)fb.value());
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
    if (used < 0) used = 0;
    if (used > cap) used = cap;
    fb.update((size_t)used, (size_t)cap);
    ring_q14 += (int64_t)fb.value(); /* host delivers the reported average */
    ring_q14 -= consume_q14;
    zassert_true(ring_q14 > 0, "ring underran at frame %d", i);
    zassert_true((ring_q14 >> 14) < cap, "ring overran at frame %d", i);
  }
  int32_t final_used = (int32_t)(ring_q14 >> 14);
  zassert_true(final_used > setp - 50 && final_used < setp + 50,
               "ring not centered near set point: %d", final_used);
}
```

- [ ] **Step 4: Write the test project scaffold**

Create `tests/unit_audio/CMakeLists.txt`:

```cmake
# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(unit_audio)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../app/src)

target_sources(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src/main.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/feedback.cpp
)
```

Create `tests/unit_audio/prj.conf`:

```
# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

CONFIG_ZTEST=y

CONFIG_CPP=y
CONFIG_STD_CPP20=y
CONFIG_EXTERNAL_LIBCPP=y

CONFIG_LOG=y
```

Create `tests/unit_audio/testcase.yaml`:

```yaml
# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

tests:
  fm.unit_audio.feedback:
    platform_allow:
      - native_sim/native/64
    tags: audio
    harness: ztest
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `west twister -T tests/unit_audio -p native_sim/native/64 -v --inline-logs`
Expected: PASS — `fm.unit_audio.feedback` with 6 ztest cases green.

- [ ] **Step 6: Enable the CI step**

In `.github/workflows/ci.yml`, replace the commented block:

```yaml
#      - name: Twister unit tests
#        working-directory: fw
#        shell: bash
#        run: |
#          set -euo pipefail
#          west twister -T tests/unit_audio -p native_sim/native/64 -v --inline-logs
```

with the enabled step:

```yaml
      - name: Twister unit tests (tests/unit_audio)
        working-directory: fw
        shell: bash
        run: |
          set -euo pipefail
          west twister -T tests/unit_audio -p native_sim/native/64 -v --inline-logs
```

- [ ] **Step 7: Format and commit**

```bash
clang-format-18 -i app/src/feedback.h app/src/feedback.cpp tests/unit_audio/src/main.cpp
cd /home/pbuchegger/oe5xrx/FW-RemoteStation
git add app/src/feedback.h app/src/feedback.cpp tests/unit_audio .github/workflows/ci.yml
git commit -m "feat(usb): add BufferFeedback PI regulator for UAC2 explicit feedback

Pure-logic Q10.14 regulator that keeps the TX ring near half full; unit-tested
on native_sim and wired into CI via the tests/unit_audio Twister step."
```

---

## Task 2: Wire explicit feedback into the bridge (OUT path)

**Files:**
- Modify: `app/src/usb_audio_bridge.cpp`
- Modify: `app/CMakeLists.txt`
- Modify: `tests/usb_audio/CMakeLists.txt`

**Interfaces:**
- Consumes (from Task 1): `usb_audio::BufferFeedback` with `init`/`reset`/`update`/`value`.
- Produces (used by Task 3): the `uac2_sof_cb` body now runs per-SOF work; Task 3 adds the IN-send block into the same callback.

- [ ] **Step 1: Add the feedback include and context members**

In `app/src/usb_audio_bridge.cpp`, add the include after the existing app includes (near line 20):

```cpp
#include "feedback.h"
```

Add a prebuffer threshold constant next to the ring-size defines (after line 45):

```cpp
/* Start draining the TX ring to the SA818 only once it is ~half full, so the
 * feedback loop has slack in both directions from the first consumed sample. */
#define TX_PREBUFFER_BYTES (TX_RING_SIZE / 2)
```

In `struct usb_audio_bridge_ctx`, add two members after the `bool rx_enabled;` line (line 99):

```cpp
  bool tx_prebuffered;             /* TX ring reached the prebuffer threshold */
  usb_audio::BufferFeedback feedback; /* explicit feedback regulator (OUT) */
```

- [ ] **Step 2: Add the feedback callback and register it**

Add this function before the `uac2_ops` definition (before line 267):

```cpp
/**
 * @brief UAC2 explicit feedback callback (OUT / playback path)
 *
 * Returns the Q10.14 samples-per-SOF the host should send so the TX ring stays
 * near half full. Only the OUT input-terminal has a feedback endpoint.
 */
static uint32_t uac2_feedback_cb(const struct device *dev, uint8_t terminal, void *user_data) {
  struct usb_audio_bridge_ctx *ctx = (struct usb_audio_bridge_ctx *)user_data;

  ARG_UNUSED(dev);

  if (terminal != USB_OUT_TERMINAL_ID) {
    return 0;
  }

  return ctx->feedback.value();
}
```

Add the callback to the ops struct (inside the `uac2_ops` initializer, after `.buf_release_cb = uac2_buf_release_cb,`):

```cpp
    .feedback_cb = uac2_feedback_cb,
```

- [ ] **Step 3: Drive the regulator from SOF and reset it on terminal changes**

Replace the body of `uac2_sof_cb` (lines 156–160) with:

```cpp
static void uac2_sof_cb(const struct device *dev, void *user_data) {
  struct usb_audio_bridge_ctx *ctx = (struct usb_audio_bridge_ctx *)user_data;

  ARG_UNUSED(dev);

  /* OUT explicit feedback: keep the TX ring near half full. */
  k_mutex_lock(&ctx->lock, K_FOREVER);
  bool tx = ctx->tx_enabled;
  size_t tx_used = ring_buf_size_get(&ctx->tx_ring) / AUDIO_BYTES_PER_SAMPLE;
  k_mutex_unlock(&ctx->lock);

  if (tx) {
    ctx->feedback.update(tx_used, TX_RING_SIZE / AUDIO_BYTES_PER_SAMPLE);
  }
}
```

In `uac2_terminal_update_cb`, replace the OUT branch (lines 173–179) with a version that resets the regulator and prebuffer state on every OUT toggle:

```cpp
  if (terminal == USB_OUT_TERMINAL_ID) {
    ctx->tx_enabled = enabled;
    ctx->tx_prebuffered = false;
    ctx->feedback.reset();
    LOG_INF("USB OUT (TX) terminal %s", enabled ? "enabled" : "disabled");

    if (!enabled) {
      ring_buf_reset(&ctx->tx_ring);
    }
  } else if (terminal == USB_IN_TERMINAL_ID) {
```

- [ ] **Step 4: Add the startup prebuffer gate to the TX drain**

Replace the body of `sa818_tx_request_cb` (lines 112–127) with:

```cpp
static size_t sa818_tx_request_cb(const struct device *dev, uint8_t *buffer, size_t size, void *user_data) {
  struct usb_audio_bridge_ctx *ctx = (struct usb_audio_bridge_ctx *)user_data;

  ARG_UNUSED(dev);

  if (!ctx->tx_enabled) {
    return 0;
  }

  k_mutex_lock(&ctx->lock, K_FOREVER);

  /* Hold off draining until the ring has prebuffered ~half full. Until then
   * emit silence so the loop has slack before the SA818 starts consuming. */
  if (!ctx->tx_prebuffered) {
    if (ring_buf_size_get(&ctx->tx_ring) >= TX_PREBUFFER_BYTES) {
      ctx->tx_prebuffered = true;
    } else {
      k_mutex_unlock(&ctx->lock);
      return 0;
    }
  }

  uint32_t bytes_read = ring_buf_get(&ctx->tx_ring, buffer, size);
  k_mutex_unlock(&ctx->lock);

  return bytes_read;
}
```

- [ ] **Step 5: Initialize the feedback + prebuffer state at registration**

In `usb_audio_bridge_register_ops`, in the "Reset state" block (lines 366–370), add:

```cpp
  ctx->tx_prebuffered = false;
  ctx->feedback.init(USB_SAMPLES_PER_SOF);
```

- [ ] **Step 6: Add feedback.cpp to both build targets**

In `app/CMakeLists.txt`, in the `elseif (CONFIG_USB_DEVICE_STACK_NEXT)` branch, extend the `target_sources` list:

```cmake
    target_sources(app PRIVATE
        src/main_usb_audio.cpp
        src/usb_audio_bridge.cpp
        src/feedback.cpp
    )
```

In `tests/usb_audio/CMakeLists.txt`, extend the `target_sources` list:

```cmake
target_sources(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/main_usb_audio.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/usb_audio_bridge.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/feedback.cpp
)
```

Also add the app/src include dir so the bridge finds `feedback.h` (after the existing `target_include_directories` in that file):

```cmake
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src
)
```

- [ ] **Step 7: Build-verify for fm_board**

Run: `west twister -T tests/usb_audio -p fm_board/stm32u575xx --build-only -v --inline-logs`
Expected: PASS — `fm.usb_audio.build` compiles with the regulator wired in. (If the local ARM toolchain is unavailable, defer this gate to CI; do not skip it silently.)

- [ ] **Step 8: Format and commit**

```bash
clang-format-18 -i app/src/usb_audio_bridge.cpp
cd /home/pbuchegger/oe5xrx/FW-RemoteStation
git add app/src/usb_audio_bridge.cpp app/CMakeLists.txt tests/usb_audio/CMakeLists.txt
git commit -m "feat(usb): explicit feedback + startup prebuffer on UAC2 OUT path

Add feedback_cb backed by BufferFeedback, drive it each SOF from the TX ring
fill, reset it on OUT terminal toggles, and hold off the SA818 drain until the
ring prebuffers ~half full."
```

---

## Task 3: SOF-driven IN capture send (remove polling thread)

**Files:**
- Modify: `app/src/usb_audio_bridge.cpp`

**Interfaces:**
- Consumes (from Task 2): the `uac2_sof_cb` body established in Task 2.
- Produces: none downstream; this completes the bridge behavior.

- [ ] **Step 1: Send one IN packet per SOF**

In `uac2_sof_cb` (as rewritten in Task 2), append the IN-capture block before the closing brace:

```cpp
  /* IN capture: send whatever whole samples we have this SOF. As an async IN
   * endpoint the variable packet size itself conveys the rate; no feedback. */
  k_mutex_lock(&ctx->lock, K_FOREVER);
  bool rx = ctx->rx_enabled;
  size_t avail = ring_buf_size_get(&ctx->rx_ring);
  size_t to_send = avail - (avail % AUDIO_BYTES_PER_SAMPLE);
  if (to_send > USB_BUF_SIZE) {
    to_send = USB_BUF_SIZE - (USB_BUF_SIZE % AUDIO_BYTES_PER_SAMPLE);
  }

  if (rx && to_send > 0) {
    uint8_t buf_idx = ctx->usb_in_buf_idx;
    ctx->usb_in_buf_idx = (ctx->usb_in_buf_idx + 1) % USB_BUF_COUNT;
    void *buf = ctx->usb_in_buf_pool[buf_idx];
    uint32_t bytes_read = ring_buf_get(&ctx->rx_ring, (uint8_t *)buf, to_send);
    k_mutex_unlock(&ctx->lock);

    /* -EAGAIN just means the host has not drained the previous IN packet yet;
     * drop silently (rate-limited for genuinely unexpected errors) so we never
     * flood the log and starve the USB thread. */
    int ret = usbd_uac2_send(ctx->uac2_dev, USB_IN_TERMINAL_ID, buf, bytes_read);
    if (ret != 0 && ret != -EAGAIN) {
      LOG_WRN_RATELIMIT("USB IN send failed: %d", ret);
    }
  } else {
    k_mutex_unlock(&ctx->lock);
  }
```

Note on context: if the target UDC rejects `usbd_uac2_send` from the SOF-callback context, fall back to submitting a lightweight `k_work` from the SOF callback that performs the same send (still SOF-paced, no busy polling). Confirm during hardware bring-up; the direct call is the default.

- [ ] **Step 2: Remove the polling thread definition and forward declaration**

Delete the forward declaration (line 105):

```cpp
static void usb_in_thread_func(void *p1, void *p2, void *p3);
```

Delete the entire `usb_in_thread_func` function (lines 278–328) and the `K_THREAD_DEFINE(usb_in_tid, ...)` line plus its comment (lines 330–333).

- [ ] **Step 3: Remove the thread start in `usb_audio_bridge_start`**

Delete the thread-start block at the end of `usb_audio_bridge_start` (lines 452–453):

```cpp
  /* Start USB IN thread now that context is fully initialized */
  k_thread_start(usb_in_tid);
```

- [ ] **Step 4: Build-verify for fm_board**

Run: `west twister -T tests/usb_audio -p fm_board/stm32u575xx --build-only -v --inline-logs`
Expected: PASS — compiles with no reference to `usb_in_tid` / `usb_in_thread_func` remaining. (Grep to confirm: `grep -n "usb_in_tid\|usb_in_thread_func" app/src/usb_audio_bridge.cpp` returns nothing.)

- [ ] **Step 5: Format and commit**

```bash
clang-format-18 -i app/src/usb_audio_bridge.cpp
cd /home/pbuchegger/oe5xrx/FW-RemoteStation
git add app/src/usb_audio_bridge.cpp
git commit -m "refactor(usb): drive UAC2 IN capture from SOF, drop polling thread

Send one variable-size async IN packet per SOF instead of a k_msleep(1) poll
loop, removing the usb_in_tid thread and its start/stop plumbing."
```

---

## Task 4: Flip the devicetree to explicit feedback + docs + HW validation

**Files:**
- Modify: `boards/oe5xrx/fm_board/fm_board.dts`
- Modify: `app/USB_AUDIO_BRIDGE.md`

**Interfaces:**
- Consumes (from Tasks 2–3): the bridge now provides `feedback_cb`, which the class requires once the OUT interface drops `implicit-feedback`.
- Produces: the final wire topology (explicit feedback endpoint on OUT, async capture on IN).

- [ ] **Step 1: Remove the `implicit-feedback` flags**

In `boards/oe5xrx/fm_board/fm_board.dts`, in `as_iso_out` (out_interface), delete the line:

```
			implicit-feedback;
```

In `as_iso_in` (in_interface), delete the line:

```
			implicit-feedback;
```

Result — both streaming nodes keep `linked-terminal`, `subslot-size = <2>`, `bit-resolution = <16>` and lose only the `implicit-feedback;` line.

- [ ] **Step 2: Verify the terminal-ID guards still hold**

The entity order (`aclk=1, usb_out=2, sa818_tx=3, sa818_rx=4, usb_in=5`) is unchanged by removing the flags, so `USB_OUT_TERMINAL_ID == 2` and `USB_IN_TERMINAL_ID == 5` and their `static_assert`s in `usb_audio_bridge.cpp` remain valid. No code change; confirm the build's static_asserts pass in Step 4.

- [ ] **Step 3: Update the bridge documentation**

In `app/USB_AUDIO_BRIDGE.md`, update the topology description to state:

- The OUT stream (host → SA818 TX) is an **asynchronous sink with an explicit
  feedback endpoint**, regulated by the software `BufferFeedback` PI controller
  over the TX ring fill (set point = half). Replaces the previous
  implicit-feedback wiring.
- The IN stream (SA818 RX → host) is a **plain asynchronous capture endpoint**
  sent one variable-size packet per SOF; this is what makes the host present it
  as a microphone with a level meter.
- Note that `feedback_cb` is mandatory for the async OUT interface once
  `implicit-feedback` is removed.

(Edit the prose to match the file's existing structure; keep it factual and brief.)

- [ ] **Step 4: Build-verify the full topology for fm_board**

Run: `west twister -T tests/usb_audio -p fm_board/stm32u575xx --build-only -v --inline-logs`
Expected: PASS — compiles with the new topology; `static_assert`s on the terminal IDs hold.

- [ ] **Step 5: Commit**

```bash
cd /home/pbuchegger/oe5xrx/FW-RemoteStation
git add boards/oe5xrx/fm_board/fm_board.dts app/USB_AUDIO_BRIDGE.md
git commit -m "feat(usb): switch UAC2 to explicit feedback + async capture on fm_board

Drop implicit-feedback from both streaming interfaces: OUT gains a class-
generated explicit feedback endpoint (serviced by feedback_cb) and IN becomes a
normal async capture stream the host shows as a microphone."
```

- [ ] **Step 6: Hardware validation on fm_board (manual — record results in the PR)**

Flash fm_board, connect USB to the Linux host, and confirm:

1. The host shows a **capture device** (`arecord -l` lists it; PulseAudio/PipeWire
   input shows a **moving input level**) — the original bug is fixed.
2. `arecord -D <dev> -f S16_LE -r 8000 -c 1 rx.wav` captures audible RX audio.
3. Playback to the device (`aplay -D <dev> -f S16_LE -r 8000 -c 1 tx.wav`) is
   transmitted by the SA818.
4. A **2-minute** continuous TX shows no ring over-/underrun in the device log
   (the feedback loop holds the buffer centered); dmesg shows no re-enumeration.

---

## Self-Review

**Spec coverage:**
- Root-cause fix (IN `implicit-feedback` → async capture): Task 4 Step 1 + validated in Task 4 Step 6.1. ✓
- OUT explicit feedback endpoint: Task 4 Step 1 (DT) + Task 2 (`feedback_cb`). ✓
- Software PI regulator over TX ring fill, Q10.14, clamp ±0.5 sample, anti-windup, LSB masking: Task 1. ✓
- Startup prebuffer: Task 2 Steps 4–5. ✓
- Ring stays at 16 ms (256 samples) — unchanged; no task grows it. ✓
- SOF-driven IN send, remove polling thread: Task 3. ✓
- native_sim unit test for the regulator + CI wiring: Task 1 Steps 3–6. ✓
- fm_board build-only gate: Tasks 2–4 Step "Build-verify". ✓
- HW validation (mic level, RX audio, TX audio, long-TX stability): Task 4 Step 6. ✓
- Non-goals (HW-timer offset, High-Speed, persistence): not implemented. ✓

**Placeholder scan:** No TBD/TODO; every code step shows complete code; every command has an expected result. ✓

**Type consistency:** `BufferFeedback::init/reset/update/value/nominal` used identically in Tasks 1 and 2. `update(used, capacity)` is called in samples in both the test (`kCapacity=256`) and the bridge (`TX_RING_SIZE / AUDIO_BYTES_PER_SAMPLE = 256`). `USB_OUT_TERMINAL_ID`/`USB_IN_TERMINAL_ID` consistent with the existing defines. ✓
