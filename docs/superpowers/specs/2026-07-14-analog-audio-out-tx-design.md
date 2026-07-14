# Hardware-Timed Analog Audio Playback Module (TX) — Design

Date: 2026-07-14
Branch: `feat/analog-audio-out-tx` (stacked on `feat/analog-audio-in-rx` / PR #57, which is
stacked on `feat/uac2-explicit-feedback` / PR #56)
Status: Approved design, pending implementation plan

## Problem

The RX capture path is now hardware-timed at 8 kHz (PR #57) and the mic works end-to-end.
The **TX path is not**: host → SA818 audio (the DAC output on PA4 that drives the SA818
mic input) still runs on the legacy per-sample `k_work` + `dac_write_value` loop in
`drivers/radio/sa818/sa818_audio_stream.cpp`, which — like the old RX path — only reaches
~2.5 kHz against an intended 8 kHz. Consequences:

- Transmitted audio is wrong (samples output at the wrong, jittery rate).
- The explicit-feedback regulator added in PR #56 (`usb_audio::BufferFeedback`, which paces
  the USB host by measuring the TX ring fill) cannot work correctly: it assumes the DAC
  consumes the ring at a stable 8 kHz, but the `k_work` DAC drains it at ~2.5 kHz, so the
  regulator sees the wrong consumption rate.

The fix is the DAC-side mirror of the RX rework: drive the DAC output from a hardware timer
with circular DMA at a true 8 kHz.

## Prior-art (why we build this)

Same conclusion as the RX design: Zephyr's STM32 DAC driver (`dac_stm32.c`) has **no**
DMA/trigger support at all — only `dac_write_value` (one sample per call). The STM32 HAL/LL
fully supports a timer-triggered, DMA-fed DAC (the classic TIM6/TIM7 → DAC pattern). We wrap
those primitives via LL + Zephyr's `dma_stm32u5` driver, exactly as the RX `analog-audio-in`
module does for the ADC.

## Goal & scope

A new reusable Zephyr driver that outputs analog audio at a hardware-clocked 8 kHz (STM32
TIM7 TRGO → DAC1 → memory→DAC circular DMA), pulling PCM from a source callback. The SA818
TX path feeds it from the bridge's TX ring. This makes transmitted audio correct and lets
the PR #56 feedback regulator operate against a real 8 kHz sink.

**Scope: the DAC audio path only.** PTT stays separate/manual (agent- or shell-controlled
via `sa818_set_ptt`); opening the USB playback stream does **not** key the transmitter, both
to keep the firmware thin and to avoid accidental transmissions. RX is unchanged.

### Non-goals (YAGNI)

- PTT / transmit keying (deliberately out of scope; stays manual/agent-controlled).
- RX / ADC changes.
- I2S/SAI/codec.
- A `native_sim` fake backend for the module (the pure conversion helper is the testable unit).

## Design

### 1. New module: `oe5xrx,analog-audio-out`

A new driver `drivers/audio/analog_audio_out/` (STM32-specific, C-first, `extern "C"` C ABI),
symmetric to `analog-audio-in` but **pull** instead of push:

```c
/* Fill up to @p max PCM samples into @p dst; return how many were provided
 * (0..max). Runs in thread context (system workqueue), so it may take a mutex. */
typedef size_t (*analog_audio_out_src)(int16_t *dst, size_t max, void *user_data);

int analog_audio_out_start(const struct device *dev, analog_audio_out_src src, void *user_data);
int analog_audio_out_stop(const struct device *dev);
```

### 2. Data flow

```
TX ring ──pull (thread)──► DMA buf[2 x block]  ──memory→DAC circular DMA──► DAC1 CH1 (PA4)
   ▲ refill the just-consumed half on half/full IRQ            ▲ TIM7 TRGO @ 8 kHz
```

- **TIM7** at 8 kHz (master-mode `TRGO = UPDATE`) triggers DAC1 channel 1 conversions; a
  **circular** memory→peripheral DMA (`LL_GPDMA1_REQUEST_DAC1_CH1`) streams the buffer to the
  DAC data register. `DAC1` is on PA4 (the board's `io-channels = <&dac1 1>` audio-out).
- The DMA half/full-transfer IRQ (~every `block` samples ≈ 1 ms) runs in **ISR context**, so
  it submits a work item; the system-workqueue handler **refills** the just-consumed half by
  calling `src()` in thread context (safe to take the TX-ring mutex), converting PCM → DAC
  values with `pcm16_to_dac`.
- **Underflow → silence:** if `src()` returns fewer than `block` samples (TX ring empty / host
  paused), the module fills the remainder with the DAC mid-scale value, so the transmitted
  signal is silence rather than a click, buzz, or repeated stale block.

Note the tighter deadline vs RX: a late refill outputs a stale half (a brief glitch), not just
delayed delivery. Double buffering + the bridge's startup prebuffer and feedback regulator keep
the ring filled; a missed refill degrades to a momentary silence, never a hang.

### 3. Pure conversion helper `pcm16_to_dac`

`int16_t`/`uint16_t pcm16_to_dac(int16_t sample, uint8_t resolution)`: signed 16-bit PCM →
unsigned DAC code, i.e. `(sample + 32768)` scaled to the DAC resolution (`>> (16 - resolution)`
for `resolution < 16`, clamped), mid-scale = silence. This is the existing inline math in the
`k_work` DAC loop, extracted into a **pure, dependency-free** function (in the module,
mirroring `analog-audio-in`'s `adc_to_pcm16`) and **unit-tested on `native_sim`** in
`tests/unit_audio` (round-trip and extreme values, including that `pcm16_to_dac(0)` maps to the
DAC mid-scale that `adc_to_pcm16` treats as silence).

### 4. Devicetree (`fm_board.dts`)

New node (STM32-specific), mirroring `analog-audio-in`:

```dts
audio_out: analog-audio-out {
    compatible = "oe5xrx,analog-audio-out";
    io-channels = <&dac1 1>;             /* DAC instance + channel (PA4) */
    io-channel-names = "audio_out";
    sampling-timer = <&timers7>;         /* TIM7 master-mode TRGO = 8 kHz pacing */
    dmas = <&gpdma1 CH REQ FEATURES>;    /* circular memory->DAC DMA (DAC1_CH1) */
    dma-names = "tx";
    sampling-frequency = <8000>;
    resolution = <12>;
    block-samples = <8>;
};
```

`&timers7` needs `status = "okay"; st,mastermode = "UPDATE"; st,prescaler = <0>;`. `&gpdma1`
is already enabled by the RX work. The exact `gpdma1` channel (distinct from the RX channel),
the DAC1_CH1 request slot, and the TIM7 prescaler/ARR for 8 kHz are resolved during
implementation against the STM32U5 RM and verified on hardware. The DAC channel must be
enabled for the trigger + DMA request (LL: trigger source = TIM7 TRGO, `EnableDMAReq`,
`EnableTrigger`, `Enable`), analogous to the ADC's PCSEL/trigger setup.

### 5. SA818 TX integration

In `sa818_audio_stream.cpp`: remove the per-sample `dac_write_value` TX loop from the work
handler. Register the module's `src` callback, which pulls PCM from the existing `tx_request`
callback (→ the bridge's TX ring, via `sa818_tx_request_cb`). Start/stop the module from
`sa818_audio_stream_start/stop` (guarded by `#if DT_NODE_EXISTS(DT_NODELABEL(audio_out))`).

Because the RX path was already moved off the work handler (PR #57), removing the TX loop too
means the `audio_stream_work_handler` / its `k_work` reschedule can likely be **deleted
entirely** — both directions are now hardware-timed. Confirm no other consumer of that work
item remains (test-tone generation uses a separate `test_tone_work`, so it is unaffected).

### 6. Interaction with the explicit-feedback regulator (PR #56)

No change needed in the bridge: once the DAC consumes the TX ring at a stable 8 kHz, the
existing `BufferFeedback` regulator (which measures TX ring fill each SOF) sees the correct
consumption rate and paces the host properly. This rework is what makes that regulator behave
as designed; verifying "long TX holds the ring centered" is part of acceptance.

### 7. Testability & hardware acceptance

- **native_sim ztest** for `pcm16_to_dac` in `tests/unit_audio`, alongside `adc_to_pcm16` and
  `BufferFeedback`.
- **fm_board build-only** (`tests/usb_audio`) + native_sim app build stay green.
- **Hardware acceptance** (manual, documented in the PR):
  1. Play a tone/speech from the laptop to the "FM Transceiver Board" playback device.
  2. Key the transmitter (`sa818 ptt on` in the shell, or the agent).
  3. On a second radio in RX, the transmitted audio is clean and correct-pitch (not
     slowed/garbled as the ~2.5 kHz path would sound).
  4. A sustained (~1–2 min) TX shows the TX ring staying centered (feedback regulator working),
     no ring under-/overrun.

### 8. Files touched (anticipated)

- Create: `drivers/audio/analog_audio_out/` — driver, `Kconfig`, `CMakeLists.txt`, `README.md`,
  the `pcm16_to_dac` helper (`.h`/`.c`).
- Create: `include/oe5xrx/audio/analog_audio_out.h` — public C ABI.
- Create: `dts/bindings/audio/oe5xrx,analog-audio-out.yaml`.
- Modify: `drivers/audio/Kconfig`, `drivers/audio/CMakeLists.txt` — wire the new leaf.
- Modify: `boards/oe5xrx/fm_board/fm_board.dts` — enable `&timers7`, add the `analog-audio-out`
  node.
- Modify: `boards/oe5xrx/fm_board/fm_board_defconfig` — `CONFIG_ANALOG_AUDIO_OUT=y`.
- Modify: `drivers/radio/sa818/sa818_audio_stream.cpp` (+ `sa818_priv.h` as needed) — TX path
  consumes the module; delete the DAC `k_work` loop (and possibly the work handler).
- Modify: `tests/unit_audio` — add the `pcm16_to_dac` ztest.

## Risks / open points to confirm during implementation

- **DAC1 coexistence**: like ADC1, the SA818 node references `<&dac1 1>` (`io-channels`), so
  Zephyr's `dac_stm32` driver binds DAC1. The module drives it via LL; confirm the same benign
  coexistence as the ADC (and that no `dac_write_value` path — e.g. the test-tone generator or
  `reset_dac_to_midpoint` — runs concurrently with streaming, or guard it).
- **Exact HW resources**: the `gpdma1` channel (must differ from the RX channel), the
  `DAC1_CH1` DMA request slot, and TIM7 prescaler/ARR — resolve against the RM and verify on HW.
- **Refill latency**: confirm the system-workqueue refill keeps up at 1 ms cadence; if it
  glitches under load, use a dedicated higher-priority workqueue/thread for the refill.
- **Startup**: ensure the DMA buffer is pre-filled with mid-scale (silence) before enabling the
  DAC trigger, so nothing spurious is emitted before the first refill.
