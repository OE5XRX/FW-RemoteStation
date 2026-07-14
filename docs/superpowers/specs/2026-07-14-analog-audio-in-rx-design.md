# Hardware-Timed Analog Audio Capture Module (RX) — Design

Date: 2026-07-14
Branch: `feat/analog-audio-in-rx` (stacked on `feat/uac2-explicit-feedback` / PR #56, whose enumeration fix is needed to test capture end-to-end)
Status: Approved design, pending implementation plan

## Problem

Hardware validation of the UAC2 explicit-feedback rework (PR #56) confirmed the USB
topology is correct — the capture device now enumerates, the explicit feedback endpoint
is present, descriptors are exact — but **end-to-end capture does not work**. Firmware
instrumentation on `fm_board` (STM32U575) measured the root cause:

- The SA818 RX audio path in `drivers/radio/sa818/sa818_audio_stream.cpp` samples the ADC
  with a **self-rescheduling `k_work_delayable`, one blocking `adc_read_dt()` per
  invocation**, targeting 8 kHz via `k_work_reschedule(K_USEC(125))`.
- With `CONFIG_SYS_CLOCK_TICKS_PER_SEC=10000` (100 µs tick) plus per-sample ADC read and
  work-queue dispatch overhead (~400 µs/sample total), the path delivers **~2511
  samples/s, not 8000**.
- A device that advertises 8 kHz capture but delivers ~2.5 kHz starves the host →
  `pcm_read: EIO`, zero frames captured. A secondary observation (iso IN transfers
  completing only in bursts of ~2 then stalling) is consistent with the host abandoning
  the under-fed stream; a work-item send fallback did **not** change it, ruling out the
  "send from SOF context" hypothesis. See PR #56 comment for the full HW findings.

The STM32U5 ADC hardware can trivially sample at 8 kHz; the bottleneck is purely the
per-sample software loop. The fix is to move the 8 kHz sample timing into hardware.

## Prior-art research (why we build this)

A dedicated search of the Zephyr tree (v4.4.99) confirmed **no ready-to-use
hardware-timer-triggered, circular-DMA continuous ADC streaming facility exists** for
STM32 (or any in-tree ADC driver):

- `adc_stm32.c` hardcodes the software trigger (`LL_ADC_REG_TRIG_SOFTWARE`), uses
  **one-shot** DMA (`LL_ADC_REG_DMA_TRANSFER_LIMITED`), and paces repeats with a
  **kernel timer** (`ADC_CONTEXT_USES_KERNEL_TIMER`) — i.e. it would reproduce the exact
  jitter/rate problem. The ADC DT binding has no external-trigger (EXTSEL) property.
- RTIO ADC streaming (`CONFIG_ADC_STREAM`) exists but only changes result *delivery*
  framing (on-chip FIFO watermark model), not the hardware sample clock; on STM32 it is
  gated to non-DMA single-conversion-per-IRQ.
- All `drivers/audio/` DMIC drivers are digital (PDM/DFSDM), not analog SAR-ADC.

However, the **STM32 HAL fully supports the required primitives** and they are present in
the tree (`stm32u5xx_hal_adc.h`): `HAL_ADC_Start_DMA()` with `ExternalTrigConv` (a timer
TRGO) and `DMAContinuousRequests` (circular DMA). Zephyr's *portable* ADC abstraction
simply does not surface them, because audio on STM32 is normally done via I2S/SAI/DFSDM
with a codec, not the raw SAR ADC. We therefore wrap documented, long-standing HAL
primitives — not a novel algorithm.

Extending the shared `adc_stm32.c` driver (adding EXTSEL + circular DMA) was considered
and rejected: it is surgery on a shared, portable driver with ongoing upstream-merge
cost. A standalone module that owns the dedicated audio ADC is cleaner and self-contained.

## Goal & scope

Build a new, reusable Zephyr device driver that samples an analog audio input
**hardware-timed at exactly 8 kHz** (STM32 TIM → ADC → circular DMA) and delivers sample
batches to a consumer, replacing the broken per-sample `k_work` acquisition. Make the
SA818 RX path consume it so microphone capture works end-to-end at 8 kHz on `fm_board`.

**Scope: RX only.** TX (host → SA818, the DAC path) is a separate later step and stays on
its existing path unchanged for now.

### Non-goals (YAGNI)

- TX / DAC rework (separate follow-up).
- I2S/SAI/codec/DFSDM (would require a hardware change; the SA818 is analog).
- A `native_sim` fake backend for the module (the bridge stays HW-only on native_sim, as
  today; the `sim_audio` path is unchanged).
- Directly fixing the secondary iso-IN-completion observation: it is expected to resolve
  once the stream is fed at a true 8 kHz, and is re-evaluated on hardware after this lands.
- Multi-instance / multi-radio support beyond what the DT binding naturally allows.

## Design

### 1. New module: `oe5xrx,analog-audio-in`

A new driver at `drivers/audio/analog_audio_in/` (STM32-specific implementation) exposing
an idiomatic Zephyr device with a plain C ABI (`extern "C"`, `[[nodiscard]]` result codes,
consistent with the driver-layer standard):

```c
typedef void (*analog_audio_in_cb)(const int16_t *samples, size_t count, void *user_data);

int analog_audio_in_start(const struct device *dev, analog_audio_in_cb cb, void *user_data);
int analog_audio_in_stop(const struct device *dev);
```

- `start` configures and starts TIM → ADC → circular DMA; `stop` halts them.
- The callback delivers `block-samples` (e.g. 8 ≈ 1 ms) of **already-converted 16-bit PCM**
  per DMA half/full-transfer event. Conversion uses the shared pure helper (§4).
- The callback runs from the DMA transfer-complete context. If that context is an ISR, the
  driver hands the batch off via a lightweight mechanism (a per-instance `k_msgq` of
  buffer indices drained by a cooperative work item, or the callback is documented as
  ISR-context and kept minimal). The chosen hand-off is fixed in the implementation plan;
  either way the consumer callback must be safe to run without blocking.

### 2. Data flow

```
TIM6 @8kHz ──TRGO──► ADC1(ch5,PA0) ──DMA(circular)──► dma_buf[2 × block_samples]
                                          │ half/full-transfer IRQ (every block ≈ 1 ms)
                                          ▼
                     analog_audio_in: adc_to_pcm16() over the ready half → on_samples()
                                          ▼
                     SA818 RX consumer: samples → rx_ring → USB IN (bridge, unchanged)
```

Hardware performs the 8 kHz timing; the CPU handles ~1000 batch events/s instead of 8000
per-sample events. Circular DMA over `2 × block_samples` provides double buffering: while
the consumer drains one half, the DMA fills the other.

### 3. Devicetree binding (`oe5xrx,analog-audio-in`)

New binding `dts/bindings/audio/oe5xrx,analog-audio-in.yaml`. Node on `fm_board`:

```dts
audio_in: analog-audio-in {
    compatible = "oe5xrx,analog-audio-in";
    io-channels = <&adc1 5>;            /* ADC instance + channel (PA0) */
    io-channel-names = "audio_in";
    sampling-timer = <&timers6>;        /* TIM6: master-mode TRGO = 8 kHz pacing */
    dmas = <&gpdma1 CH REQ FEATURES>;   /* circular DMA channel for ADC1 */
    dma-names = "rx";
    sampling-frequency = <8000>;
    resolution = <12>;
    block-samples = <8>;                /* samples per callback ≈ 1 ms */
};
```

- `TIM6` is a basic timer, ideal for ADC pacing and otherwise unused; its `st,stm32-timers`
  node is configured for a periodic 8 kHz update event with master-mode TRGO output (the
  timer binding already supports `st,trigger-selection` / master-mode). The exact TRGO
  routing to the ADC external-trigger input is done in the driver via HAL
  (`ExternalTrigConv`).
- The precise `gpdma1` channel/request/features triple and the `timers6` prescaler/period
  are resolved during implementation against the STM32U5 reference manual and verified on
  hardware.

### 4. ADC ownership & SA818 integration

- **The module owns ADC1** for continuous timer+DMA conversion via the STM32 HAL
  (`HAL_ADC_Start_DMA` + external trigger + continuous requests) — a mode Zephyr's generic
  `adc_stm32` driver cannot provide. **Verify during implementation that ADC1 is
  audio-only** on `fm_board` (confirmed by the board author: no RSSI/battery/other
  consumer); the `io-channels = <&adc1 5>`
  reference and channel config move from the SA818 node to the `analog-audio-in` node.
  Clock enable, pinctrl (PA0), and the ADC/DMA IRQ wiring are set up by the module using
  the standard Zephyr STM32 mechanisms (`clock_control`, `pinctrl`, `IRQ_CONNECT`).
- **SA818 RX path change** (`sa818_audio_stream.cpp`): the RX branch of
  `audio_stream_work_handler` (blocking `adc_read_dt` per tick) is removed. Instead the
  stream registers the module's `on_samples` callback and pushes the delivered PCM samples
  into `rx_ring` (the same data the bridge's `sa818_rx_data_cb` consumes today, now
  hardware-clocked). `sa818_audio_enable_path(rx, tx)`: enabling RX now
  `analog_audio_in_start()`s the module; disabling RX stops it.
- **TX path unchanged**: the DAC write path (whether it stays on `k_work` or elsewhere) is
  untouched by this change and remains an acceptable intermediate state until the separate
  TX rework.

### 5. Testability

- **Pure conversion helper** `adc_to_pcm16(uint16_t raw, uint8_t resolution) -> int16_t`
  (offset the ADC midpoint, scale to signed 16-bit; the same math currently inline in
  `sa818_audio_stream.cpp`) is extracted into a standalone, dependency-free function and
  **unit-tested on `native_sim`** (ztest, added to the existing `tests/unit_audio`
  project alongside `BufferFeedback`). This is the extractable logic.
- **The TIM/ADC/DMA low-level (LL) code is genuine hardware setup** and is verified **on the board**.
  Per `CLAUDE.md`, this is acceptable because the hardware-specific code is cleanly
  encapsulated in the module and the surrounding logic (conversion, ring buffering, bridge)
  stays testable off-hardware.
- **`native_sim`**: the module's binding is STM32-specific and is not instantiated on
  `native_sim`; the app there keeps using the existing `sim_audio` path, and the USB bridge
  remains HW-only on `native_sim` exactly as today. No regression to CI's native_sim gates.
- **Hardware acceptance criteria** (reproduced via the same instrumentation method used
  during diagnosis, then reverted before merge):
  1. Measured RX production rate is **8000 ± a few samples/s** (not ~2500).
  2. `arecord` on the board's capture device runs without immediate `EIO` and produces a
     WAV with **non-silent, real audio content** (peak/RMS > 0, varying samples).
  3. Sustained multi-second capture without repeated stream teardown.
  4. Re-evaluate the iso-IN-completion behaviour now that the stream is fed at 8 kHz.

### 6. Files touched (anticipated)

- Create: `drivers/audio/analog_audio_in/` — driver source (STM32 HAL impl), `CMakeLists.txt`,
  `Kconfig`; and `drivers/audio/` wiring if not present.
- Create: `dts/bindings/audio/oe5xrx,analog-audio-in.yaml` — the binding.
- Create: `include/...` public header for the module API (plain-C ABI).
- Create: a shared `adc_to_pcm16` helper (header + small source, placed so both the driver
  and the native_sim unit test can include it) + its ztest in `tests/unit_audio`.
- Modify: `boards/oe5xrx/fm_board/fm_board.dts` — add the `analog-audio-in` node + `&timers6`
  (+ `&gpdma1` channel) config; move the audio-in ADC channel off the SA818 node.
- Modify: `drivers/radio/sa818/sa818_audio_stream.cpp` (+ `sa818_priv.h` as needed) — RX
  acquisition consumes the module instead of `adc_read_dt`; TX path untouched.
- Modify: Kconfig/defconfig for the new driver + required `CONFIG_DMA`, HAL ADC module, etc.

## Risks / open points to confirm during implementation

- **ADC1 is audio-dedicated** on `fm_board` — confirmed by the board author (audio-only,
  no other consumer), so the module can take it over cleanly.
- **Exact HW resource triple**: the `gpdma1` channel/request for ADC1 and the `timers6`
  TRGO routing / prescaler+period for 8 kHz — resolve against the STM32U5 RM and verify on
  hardware.
- **Callback context**: confirm whether the module's `on_samples` runs in ISR or thread
  context on this UDC/DMA setup and choose the hand-off accordingly (§1).
- **HAL vs LL mix**: using the STM32 HAL ADC/DMA handles alongside Zephyr's clock/pinctrl —
  confirm the HAL ADC module is enabled and there is no ownership conflict with the Zephyr
  `adc_stm32` driver over `adc1` (likely the Zephyr ADC driver must not also bind `adc1`).
