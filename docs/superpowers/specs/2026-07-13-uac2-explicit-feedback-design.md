# UAC2 Explicit Feedback + Separate RX/TX Streams — Design

Date: 2026-07-13
Branch: `feat/uac2-explicit-feedback` (based on `origin/main`)
Status: Approved design, pending implementation plan

## Problem

The 3-class USB composite now enumerates on `fm_board` (PR #55), but the UAC2 audio
bridge does not work as an audio device:

- **Primary symptom (observed on the Linux host):** the capture direction
  (SA818 RX → host, i.e. the "microphone") is not usable. No level is shown in the
  host's volume/input control and no audio arrives. A prior investigation confirmed
  that **no samples currently reach the host over the UAC2 IN stream**.
- The current topology couples the two directions in a way that does not match the
  device: an FM radio is effectively **half-duplex / two independent streams**
  (transmit *or* receive), not a synchronized duplex codec.

### Confirmed root cause

`boards/oe5xrx/fm_board/fm_board.dts` declares **both** UAC2 audio-streaming
interfaces with the `implicit-feedback` property:

```
as_iso_out: out_interface { linked-terminal = <&usb_out_terminal>; implicit-feedback; ... }
as_iso_in:  in_interface  { linked-terminal = <&usb_in_terminal>;  implicit-feedback; ... }
```

Per the Zephyr binding `zephyr,uac2-audio-streaming.yaml`:

> `implicit-feedback`: For **IN** endpoints this sets endpoint behaviour type to
> *implicit feedback data endpoint*. For **OUT** endpoints setting this property
> *removes* the explicit feedback endpoint.

So the IN endpoint currently advertises itself to the host as an **implicit-feedback
data endpoint** (a clock/rate source for a paired OUT stream) rather than as a normal
asynchronous capture stream. That is why the host does not present it as a microphone
with a level meter and why no capture samples flow.

### Clock domain — why this is not the nRF sample

The official Zephyr `uac2_explicit_feedback` sample measures the offset between the
I2S FRAMESTART and the USB SOF using **nRF-specific hardware** (DPPI + TIMER + GPIOTE).
The STM32U575 (`fm_board`) has no equivalent wiring, and our audio sink/source is not
a hardware codec clock at all: `drivers/radio/sa818/sa818_audio_stream.cpp` drives the
ADC/DAC from a **software `k_work_delayable`** that reschedules itself every
`1_000_000 / sample_rate` µs (125 µs @ 8 kHz). The audio "clock" is therefore the
Zephyr kernel timer, asynchronous to the USB host SOF and subject to the usual
crystal drift.

Because the sink is software-timed **and already fed through a ring buffer**, we can
close the feedback loop entirely in software using the **TX ring-buffer fill level**
as the process variable — no hardware timers required.

## Goal

Rebuild the UAC2 audio bridge into **two independent asynchronous streams**:

- **OUT** (host → SA818 TX): asynchronous sink with an **explicit feedback endpoint**,
  regulated by a software PI controller over the TX ring-buffer fill level.
- **IN** (SA818 RX → host): a plain **asynchronous capture** endpoint. The variable
  packet size per SOF conveys the true rate (async IN needs no feedback endpoint).

## Non-goals (YAGNI)

- Hardware-timer SOF-offset measurement (the nRF sample's approach).
- High-Speed support — `fm_board` is Full-Speed only (`full-speed;`, no `high-speed;`),
  so only the FS Q10.14 feedback format is implemented.
- Capability persistence, multi-radio support, or any platform/access logic (stays out
  of firmware by design, per `CONTRIBUTING.md` / `CLAUDE.md`).

## Design

### 1. Devicetree (`boards/oe5xrx/fm_board/fm_board.dts`, hardware only)

- Remove `implicit-feedback` from **both** `as_iso_out` and `as_iso_in`.
  - OUT without the flag → the UAC2 class **automatically adds the explicit feedback
    endpoint**.
  - IN without the flag → normal asynchronous capture endpoint (host shows a mic with
    a level).
- Clock source `uac_aclk` is unchanged: `clock-type = "internal-fixed"`,
  `frequency-control = "read-only"`, `sampling-frequencies = <8000>`. This is already
  an asynchronous internal clock, which is the correct prerequisite for explicit
  feedback.

Note: the `uac2_radio` node exists **only** in `fm_board.dts`. On `native_sim` the
UAC2 topology is compiled out (`main_usb_audio.cpp` guards it behind
`#if DT_NODE_EXISTS(UAC2_NODE)`). The DT change is therefore hardware-only, and
end-to-end UAC2 behaviour can only be verified on `fm_board`.

### 2. Feedback regulator — new, USB-free, native_sim-testable unit

New class `BufferFeedback` in `app/src/feedback.h` / `app/src/feedback.cpp`
(non-driver code → modern C++20, no heap, no float, no exceptions per the coding
standard). API mirrors the sample's `feedback.h` intent:

- `void reset();` — reset integrator, set feedback to nominal.
- `void update(size_t used, size_t capacity);` — run one PI step from the current TX
  ring fill.
- `uint32_t value() const;` — current feedback value to report to the host.

Details:

- Full-Speed feedback format is **Q10.14** (Q10.10 left-justified in 24 bits). Nominal
  value for 8 samples/SOF @ 8 kHz is `8 << 14`.
- Fixed-point PI controller. Process variable = TX ring fill; set point = half
  capacity; error drives an integrator plus proportional term.
- The output is **clamped** to a narrow band around nominal (≈ ±0.5 sample) so the host
  only ever sends nominal ±1 sample per frame. The low LSBs are cleared to match the
  sample's convention of not using the optional extra resolution.
- No hardware, no USB dependency → unit-testable in isolation.

### 3. Bridge changes (`app/src/usb_audio_bridge.cpp`)

- Add `feedback_cb` to `uac2_ops`; for the OUT terminal it returns `regulator.value()`.
- In `uac2_sof_cb` (fires every SOF, ~1 ms): when TX is enabled, read the TX ring fill
  under the existing lock and call `regulator.update(...)`. Call `regulator.reset()`
  when the OUT terminal is disabled (in `uac2_terminal_update_cb`).
- **IN path:** replace the fragile `k_msleep(1)` polling thread with **SOF-driven
  sending** inside `uac2_sof_cb`: once per frame, send the available whole samples
  (capped at a max packet size) via `usbd_uac2_send`. As an asynchronous IN endpoint,
  the variable packet size itself carries the rate; `-EAGAIN` continues to be dropped
  silently (the existing anti-log-flood behaviour is preserved). This removes the
  separate `usb_in_tid` thread and its start/stop plumbing.

The terminal-ID discovery constants (`USB_OUT_TERMINAL_ID`, `USB_IN_TERMINAL_ID`) and
their build-time `static_assert`s are re-verified against the regenerated descriptors
after the DT change (adding/removing the feedback endpoint does not change entity IDs,
but the guard must be confirmed).

### 4. Testing

- **native_sim ztest** for `BufferFeedback`: drive it with synthetic fill-level
  sequences representing "host too fast" and "host too slow" drift; assert the feedback
  value moves in the correct direction, converges toward keeping the ring near half
  full, and stays within the clamp band. Pure logic, no USB.
- **`tests/usb_audio`**: review and extend to cover whatever bridge logic is
  exercisable on native_sim after the refactor.
- **`fm_board` hardware validation** (manual, documented in the PR):
  1. Host shows a capture device with a moving input level.
  2. RX audio is audible on the host.
  3. TX audio (host → radio) works.
  4. Long-running TX shows no ring over-/underrun (feedback holds the buffer centered).

### 5. Files touched (anticipated)

- `boards/oe5xrx/fm_board/fm_board.dts` — remove the two `implicit-feedback` flags.
- `app/src/feedback.h`, `app/src/feedback.cpp` — new `BufferFeedback` regulator.
- `app/src/usb_audio_bridge.cpp` — `feedback_cb`, SOF-driven update + IN send, remove
  polling thread.
- `app/CMakeLists.txt` — add `feedback.cpp`.
- Test files — new native_sim ztest for the regulator; `tests/usb_audio` touch-ups.
- Docs — `app/USB_AUDIO_BRIDGE.md` updated to describe the async/explicit-feedback
  topology.

## Risks / open points to confirm during implementation

- Whether `usbd_uac2_send` may be called directly from the SOF callback context on the
  STM32 UDC driver. If it cannot, the IN send falls back to a lightweight work item
  submitted from the SOF callback (still SOF-paced, no busy polling).
- PI gains: ring-level control is forgiving, but initial gains are validated on
  hardware and may be tuned.
