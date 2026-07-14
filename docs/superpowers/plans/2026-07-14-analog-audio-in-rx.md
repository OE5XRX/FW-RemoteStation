# Hardware-Timed Analog Audio Capture (RX) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Sample the SA818 analog RX audio at a true, hardware-clocked 8 kHz (STM32 TIM6 TRGO → ADC1 → circular DMA) via a new reusable `oe5xrx,analog-audio-in` driver, so USB microphone capture works end-to-end on `fm_board`.

**Architecture:** A new Zephyr driver at `drivers/audio/analog_audio_in/` configures TIM6 to emit an 8 kHz TRGO, ADC1 (via LL) to convert on that trigger into a circular DMA buffer managed by Zephyr's `dma_stm32u5` driver; half/full-transfer callbacks convert each batch to 16-bit PCM and hand it to a consumer callback. The SA818 RX path consumes that callback instead of its broken per-sample `k_work`+`adc_read_dt`.

**Tech Stack:** Zephyr RTOS (v4.4.99), STM32U575, STM32 LL (ADC + TIM), Zephyr `dma_stm32u5` DMA API, devicetree, C++/C driver layer, ztest (native_sim).

## Nature of this plan (read first)

Tasks 1–2 are deterministic (a pure helper + scaffolding) and follow strict TDD/build-only gates. **Tasks 3–4 are STM32 hardware bring-up**: the register/LL/DMA sequences below are concrete and derived from the STM32U5 reference patterns, but peripheral bring-up (ADC calibration/enable, TRGO-triggered DMA, GPDMA circular emulation, OVR handling) routinely needs on-hardware iteration. Each hardware task therefore ends in an **on-board measurement**, not a unit test, and is structured as incremental, independently observable stages. Where a value is known-uncertain it is flagged inline as "verify on HW" — that is deliberate, not a placeholder.

Build/flash environment: `west` in the venv `~/zephyr-oe5xrx` (`source ~/zephyr-oe5xrx/bin/activate`), ARM SDK `~/zephyr-sdk-1.0.1`. Build: `west build -b fm_board/stm32u575xx app --build-dir build_fm`. Flash: `west flash --build-dir build_fm --runner pyocd`. Console: `/dev/ttyACM0` @115200. Capture test host-side: `pasuspender -- arecord -D hw:<card>,0 -f S16_LE -c 1 -r 8000 -d 4 out.wav` (find `<card>` via `arecord -l`).

## Global Constraints

- Driver layer = C-first Zephyr idiom: `struct device`, DT (`DT_NODELABEL`/`DT_INST_*`), `extern "C"` public headers, C-enum result types, `[[nodiscard]]` on functions returning a result code. Modern C++ inside a TU is allowed; the ABI stays plain C.
- No dynamic allocation, no exceptions, no RTTI. Fixed-size buffers only.
- Full-Speed audio path is 8 kHz, 16-bit, mono. Nominal 8 samples/SOF.
- ADC1 is audio-only on `fm_board` (confirmed by the board author) — the new driver takes it over.
- clang-format-18 must produce no diff on any `.c/.h/.cpp/.hpp` under `app/`, `boards/`, `tests/`, `drivers/`.
- Real test dirs only: `tests/etl`, `tests/sim_shell`, `tests/usb_audio`, `tests/unit_audio`. The pure helper's ztest goes in `tests/unit_audio`.
- Known HW values (from research, STM32U575 @ 160 MHz core, APB1 prescaler 1):
  - ADC1 trigger: `LL_ADC_REG_SetTriggerSource(ADC1, LL_ADC_REG_TRIG_EXT_TIM6_TRGO)`, `LL_ADC_REG_SetTriggerEdge(ADC1, LL_ADC_REG_TRIG_EXT_RISING)`.
  - ADC1 DMA mode (U5): `LL_ADC_REG_SetDataTransferMode(ADC1, LL_ADC_REG_DMA_TRANSFER_LIMITED)`.
  - GPDMA request slot for ADC1: `LL_GPDMA1_REQUEST_ADC1` == `0`.
  - TIM6 clock = 160 MHz (get at runtime via `clock_control_get_rate` on the `STM32_SRC_TIMPCLK1` clock cell; do not hardcode). For 8 kHz: `PSC=0`, `ARR=19999` (`(0+1)*(19999+1)=20000`, `160e6/20000=8000`). TRGO = update: `st,mastermode = "UPDATE"` → `LL_TIM_TRGO_UPDATE`.
  - `&gpdma1` (compatible `st,stm32u5-dma`) and `&timers6` (compatible `st,stm32-timers`) are `status = "disabled"` in the SoC dtsi — both must be enabled in `fm_board.dts`.
  - GPDMA circular capture: use Zephyr `dma_stm32u5` via `dma_config`+`dma_block_config` with `head_block.source_reload_en = 1` (GPDMA has no native circular register; the driver emulates it). Callback status: half-transfer → `DMA_STATUS_BLOCK`, full-transfer → `DMA_STATUS_COMPLETE`.
  - `dmas` DT cell format (`#dma-cells = <3>`): `<&gpdma1 CHANNEL SLOT CONFIG>`; e.g. `<&gpdma1 0 0 (STM32_DMA_16BITS | STM32_DMA_PRIORITY_HIGH)>` (config bits from `dt-bindings/dma/stm32_dma.h`).

---

## File Structure

**Create:**
- `drivers/audio/Kconfig` — `menu "Audio drivers"` + `rsource "analog_audio_in/Kconfig"`.
- `drivers/audio/CMakeLists.txt` — `add_subdirectory_ifdef(CONFIG_ANALOG_AUDIO_IN analog_audio_in)`.
- `drivers/audio/analog_audio_in/Kconfig` — `config ANALOG_AUDIO_IN`.
- `drivers/audio/analog_audio_in/CMakeLists.txt` — `zephyr_library()` + sources.
- `drivers/audio/analog_audio_in/analog_audio_in.c` — the STM32 driver.
- `include/oe5xrx/audio/analog_audio_in.h` — public C ABI (start/stop + callback typedef).
- `drivers/audio/analog_audio_in/adc_pcm.h` + `adc_pcm.c` — the pure `adc_to_pcm16` helper (no Zephyr/HW deps), placed so both the driver and the native_sim test include it.
- `dts/bindings/audio/oe5xrx,analog-audio-in.yaml` — the DT binding.

**Modify:**
- `drivers/Kconfig` — add `rsource "audio/Kconfig"`.
- `drivers/CMakeLists.txt` — add `add_subdirectory(audio)`.
- `boards/oe5xrx/fm_board/fm_board.dts` — enable `&gpdma1` + `&timers6`; add the `analog_audio_in` node; remove the audio-in `io-channels` entry from the `sa818` node.
- `boards/oe5xrx/fm_board/fm_board_defconfig` (or `app/prj.conf`) — `CONFIG_DMA=y`, `CONFIG_ANALOG_AUDIO_IN=y`.
- `drivers/radio/sa818/sa818_audio_stream.cpp` (+ `sa818_priv.h`) — RX path consumes the module; drop the per-sample `adc_read_dt`.
- `tests/unit_audio/src/main.cpp` + `tests/unit_audio/CMakeLists.txt` — add the `adc_to_pcm16` ztest + source.

---

## Task 1: Pure `adc_to_pcm16` conversion helper + native_sim ztest

**Files:**
- Create: `drivers/audio/analog_audio_in/adc_pcm.h`, `drivers/audio/analog_audio_in/adc_pcm.c`
- Modify: `tests/unit_audio/src/main.cpp`, `tests/unit_audio/CMakeLists.txt`

**Interfaces:**
- Produces (consumed by Task 3): `int16_t adc_to_pcm16(uint16_t raw, uint8_t resolution);` — left-justify the unsigned ADC reading to full 16-bit range then subtract the midpoint, yielding signed 16-bit PCM. Matches the existing inline logic in `sa818_audio_stream.cpp` (up-shift by `16 - resolution`, subtract 32768).

- [ ] **Step 1: Write the header**

Create `drivers/audio/analog_audio_in/adc_pcm.h`:

```c
/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef OE5XRX_ANALOG_AUDIO_IN_ADC_PCM_H_
#define OE5XRX_ANALOG_AUDIO_IN_ADC_PCM_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Convert one unsigned ADC reading to signed 16-bit PCM.
 *
 * The reading is left-justified from @p resolution bits up to the full 16-bit
 * range, then offset by the midpoint so silence (mid-scale) maps to 0.
 * @p resolution is clamped to [1, 16].
 */
int16_t adc_to_pcm16(uint16_t raw, uint8_t resolution);

#ifdef __cplusplus
}
#endif

#endif /* OE5XRX_ANALOG_AUDIO_IN_ADC_PCM_H_ */
```

- [ ] **Step 2: Write the failing tests**

Add to `tests/unit_audio/src/main.cpp` (keep existing suites):

```cpp
#include "adc_pcm.h"

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
```

Add the helper source + its include dir to `tests/unit_audio/CMakeLists.txt`:

```cmake
target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../drivers/audio/analog_audio_in)

target_sources(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src/main.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/feedback.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../drivers/audio/analog_audio_in/adc_pcm.c
)
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `west twister -T tests/unit_audio -p native_sim/native/64 -v --inline-logs`
Expected: FAIL — `adc_pcm.c` has no definition / link error for `adc_to_pcm16`.

- [ ] **Step 4: Write the implementation**

Create `drivers/audio/analog_audio_in/adc_pcm.c`:

```c
/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "adc_pcm.h"

int16_t adc_to_pcm16(uint16_t raw, uint8_t resolution) {
  if (resolution < 1) {
    resolution = 1;
  } else if (resolution > 16) {
    resolution = 16;
  }
  uint8_t up_shift = (uint8_t)(16 - resolution);
  int32_t full = ((int32_t)raw << up_shift) - 32768;
  return (int16_t)full;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `west twister -T tests/unit_audio -p native_sim/native/64 -v --inline-logs`
Expected: PASS — all `adc_pcm` cases green plus the existing `feedback` suite.

- [ ] **Step 6: Format and commit**

```bash
clang-format-18 -i drivers/audio/analog_audio_in/adc_pcm.h drivers/audio/analog_audio_in/adc_pcm.c tests/unit_audio/src/main.cpp
cd /home/pbuchegger/oe5xrx/FW-RemoteStation
git add drivers/audio/analog_audio_in/adc_pcm.h drivers/audio/analog_audio_in/adc_pcm.c tests/unit_audio
git commit -m "feat(audio): add adc_to_pcm16 helper + native_sim tests"
```

---

## Task 2: Driver scaffolding, DT binding, and build wiring (no hardware behavior yet)

**Files:**
- Create: `include/oe5xrx/audio/analog_audio_in.h`, `drivers/audio/analog_audio_in/analog_audio_in.c`, `drivers/audio/analog_audio_in/Kconfig`, `drivers/audio/analog_audio_in/CMakeLists.txt`, `drivers/audio/Kconfig`, `drivers/audio/CMakeLists.txt`, `dts/bindings/audio/oe5xrx,analog-audio-in.yaml`
- Modify: `drivers/Kconfig`, `drivers/CMakeLists.txt`, `boards/oe5xrx/fm_board/fm_board.dts`, `boards/oe5xrx/fm_board/fm_board_defconfig`

**Interfaces:**
- Produces (consumed by Tasks 3–4):
  - `typedef void (*analog_audio_in_cb)(const int16_t *samples, size_t count, void *user_data);`
  - `int analog_audio_in_start(const struct device *dev, analog_audio_in_cb cb, void *user_data);`
  - `int analog_audio_in_stop(const struct device *dev);`

- [ ] **Step 1: Public header**

Create `include/oe5xrx/audio/analog_audio_in.h`:

```c
/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef OE5XRX_AUDIO_ANALOG_AUDIO_IN_H_
#define OE5XRX_AUDIO_ANALOG_AUDIO_IN_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Delivers a batch of converted 16-bit PCM samples. May run in IRQ context. */
typedef void (*analog_audio_in_cb)(const int16_t *samples, size_t count, void *user_data);

/** Start hardware-timed capture; @p cb is invoked once per DMA half/full block. */
int analog_audio_in_start(const struct device *dev, analog_audio_in_cb cb, void *user_data);

/** Stop capture. */
int analog_audio_in_stop(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* OE5XRX_AUDIO_ANALOG_AUDIO_IN_H_ */
```

- [ ] **Step 2: DT binding**

Create `dts/bindings/audio/oe5xrx,analog-audio-in.yaml`:

```yaml
# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

description: |
  Hardware-timed analog audio capture: a timer TRGO drives periodic ADC
  conversions into a circular DMA buffer. STM32-specific.

compatible: "oe5xrx,analog-audio-in"

include: base.yaml

properties:
  io-channels:
    required: true
    description: ADC instance + channel used for the audio input.
  sampling-timer:
    type: phandle
    required: true
    description: Timer node whose TRGO paces conversions (master-mode = UPDATE).
  dmas:
    required: true
  dma-names:
    required: true
  sampling-frequency:
    type: int
    required: true
    description: Sample rate in Hz (e.g. 8000).
  resolution:
    type: int
    required: true
    description: ADC resolution in bits (e.g. 12).
  block-samples:
    type: int
    required: true
    description: Samples per DMA half-buffer / per consumer callback.
```

- [ ] **Step 3: Driver skeleton**

Create `drivers/audio/analog_audio_in/analog_audio_in.c` — config parsed from DT, `start`/`stop` present but not yet touching hardware (Task 3 fills them):

```c
/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#define DT_DRV_COMPAT oe5xrx_analog_audio_in

#include <oe5xrx/audio/analog_audio_in.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(analog_audio_in, CONFIG_ANALOG_AUDIO_IN_LOG_LEVEL);

struct aai_config {
  uint32_t sampling_frequency;
  uint16_t block_samples;
  uint8_t resolution;
};

struct aai_data {
  analog_audio_in_cb cb;
  void *user_data;
  bool running;
};

int analog_audio_in_start(const struct device *dev, analog_audio_in_cb cb, void *user_data) {
  struct aai_data *data = dev->data;

  if (cb == NULL) {
    return -EINVAL;
  }
  data->cb = cb;
  data->user_data = user_data;
  data->running = true;
  LOG_INF("start (hardware path added in a later step)");
  return 0;
}

int analog_audio_in_stop(const struct device *dev) {
  struct aai_data *data = dev->data;

  data->running = false;
  return 0;
}

static int aai_init(const struct device *dev) {
  const struct aai_config *cfg = dev->config;

  LOG_INF("init: %u Hz, %u-bit, block=%u", cfg->sampling_frequency, cfg->resolution, cfg->block_samples);
  return 0;
}

#define AAI_INIT(inst)                                                                              \
  static const struct aai_config aai_cfg_##inst = {                                                \
      .sampling_frequency = DT_INST_PROP(inst, sampling_frequency),                                 \
      .block_samples = DT_INST_PROP(inst, block_samples),                                           \
      .resolution = DT_INST_PROP(inst, resolution),                                                 \
  };                                                                                                \
  static struct aai_data aai_data_##inst;                                                           \
  DEVICE_DT_INST_DEFINE(inst, aai_init, NULL, &aai_data_##inst, &aai_cfg_##inst, POST_KERNEL,       \
                        CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(AAI_INIT)
```

- [ ] **Step 4: Kconfig + CMake wiring (mirror `drivers/radio/sa818`)**

Create `drivers/audio/analog_audio_in/Kconfig`:

```
# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

config ANALOG_AUDIO_IN
	bool "Hardware-timed analog audio capture (TIM+ADC+DMA)"
	default y
	depends on DT_HAS_OE5XRX_ANALOG_AUDIO_IN_ENABLED
	select DMA
	help
	  Timer-TRGO-triggered ADC + circular DMA analog audio capture driver.

if ANALOG_AUDIO_IN
module = ANALOG_AUDIO_IN
module-str = analog_audio_in
source "subsys/logging/Kconfig.template.log_config"
endif
```

Create `drivers/audio/analog_audio_in/CMakeLists.txt`:

```cmake
# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later
zephyr_library()
zephyr_library_sources(analog_audio_in.c adc_pcm.c)
zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../include)
```

Create `drivers/audio/Kconfig`:

```
# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later
menu "Audio drivers"
rsource "analog_audio_in/Kconfig"
endmenu
```

Create `drivers/audio/CMakeLists.txt`:

```cmake
# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later
add_subdirectory_ifdef(CONFIG_ANALOG_AUDIO_IN analog_audio_in)
```

In `drivers/Kconfig`, add after the existing `rsource` lines:

```
rsource "audio/Kconfig"
```

In `drivers/CMakeLists.txt`, add:

```cmake
add_subdirectory(audio)
```

- [ ] **Step 5: Devicetree — enable GPDMA1 + TIM6, add the node, take ADC1**

In `boards/oe5xrx/fm_board/fm_board.dts`:

Enable the peripherals (add near the other `&`-overrides):

```dts
&gpdma1 {
	status = "okay";
};

&timers6 {
	status = "okay";
	st,mastermode = "UPDATE";
};
```

Add the capture node (top-level `/`), and **remove** the `<&adc1 5>` entry from the `sa818` node's `io-channels` (it moves here):

```dts
	audio_in: analog-audio-in {
		compatible = "oe5xrx,analog-audio-in";
		io-channels = <&adc1 5>;
		io-channel-names = "audio_in";
		sampling-timer = <&timers6>;
		dmas = <&gpdma1 0 0 (STM32_DMA_16BITS | STM32_DMA_PRIORITY_HIGH)>;
		dma-names = "rx";
		sampling-frequency = <8000>;
		resolution = <12>;
		block-samples = <8>;
	};
```

Add the DMA dt-bindings include at the top of the dts if not present:

```dts
#include <zephyr/dt-bindings/dma/stm32_dma.h>
```

In `boards/oe5xrx/fm_board/fm_board_defconfig`, add:

```
CONFIG_DMA=y
CONFIG_ANALOG_AUDIO_IN=y
```

- [ ] **Step 6: Build-verify for fm_board**

Run: `source ~/zephyr-oe5xrx/bin/activate && west build -b fm_board/stm32u575xx app --build-dir build_fm -p always 2>&1 | tail -20`
Expected: builds clean; boot log (after flash, optional here) shows `analog_audio_in: init: 8000 Hz, 12-bit, block=8`. If `&gpdma1`/`&timers6`/`dmas` cause DT errors, resolve the cell/status before proceeding.

- [ ] **Step 7: Format and commit**

```bash
clang-format-18 -i drivers/audio/analog_audio_in/analog_audio_in.c include/oe5xrx/audio/analog_audio_in.h
cd /home/pbuchegger/oe5xrx/FW-RemoteStation
git add drivers/audio include/oe5xrx dts/bindings/audio drivers/Kconfig drivers/CMakeLists.txt boards/oe5xrx/fm_board/fm_board.dts boards/oe5xrx/fm_board/fm_board_defconfig
git commit -m "feat(audio): scaffold oe5xrx,analog-audio-in driver + DT wiring"
```

---

## Task 3: Hardware bring-up — TIM6 8 kHz TRGO → ADC1 → circular DMA → callback

**Files:**
- Modify: `drivers/audio/analog_audio_in/analog_audio_in.c`

**Interfaces:**
- Consumes (Task 1): `adc_to_pcm16()`. Consumes (Task 2): the driver skeleton, config, `analog_audio_in_cb`.
- Produces (Task 4): a working `analog_audio_in_start/stop` that delivers 8000 PCM samples/s via the callback.

This is hardware bring-up: build it in stages, flashing and observing after each. Add temporary per-second instrumentation (a batch/sample counter logged once per ~8000 samples) to measure the real rate, exactly as used during diagnosis; remove it in Task 5.

- [ ] **Step 1: Add includes, DMA/timer/ADC handles to config+data**

Extend `analog_audio_in.c`:

```c
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_stm32.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <stm32_ll_adc.h>
#include <stm32_ll_tim.h>
#include <stm32_ll_bus.h>
```

Add to `struct aai_config`: the ADC base, TIM base, timer clock spec, DMA device+channel, and the ADC/TIM clock control subsystems, parsed from DT. Add to `struct aai_data`: the DMA buffer `int16_t dma_buf[2 * block_samples]` (fixed max, e.g. `AAI_MAX_BLOCK 16` → `dma_buf[2*AAI_MAX_BLOCK]`), a `int16_t pcm_buf[AAI_MAX_BLOCK]` scratch, and the `struct dma_config`/`struct dma_block_config`.

Concrete resource resolution (from research):
- ADC1 base: `(ADC_TypeDef *)DT_REG_ADDR(DT_INST_IO_CHANNELS_CTLR(inst))`.
- TIM6 base: `(TIM_TypeDef *)DT_REG_ADDR(DT_INST_PHANDLE(inst, sampling_timer))`.
- TIM6 clock cells: `STM32_DT_CLOCKS(DT_INST_PHANDLE(inst, sampling_timer))`; get rate via `clock_control_get_rate(clk, &pclken[1])` on the `STM32_SRC_TIMPCLK1` cell (mirror `counter_stm32_timer.c:426-438`).
- DMA device + channel: `DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(inst, rx))`, `DT_INST_DMAS_CELL_BY_NAME(inst, rx, channel)`, slot `DT_INST_DMAS_CELL_BY_NAME(inst, rx, slot)`, config `DT_INST_DMAS_CELL_BY_NAME(inst, rx, channel_config)`.

- [ ] **Step 2: Stage A — TIM6 8 kHz TRGO alone**

In a new `static int aai_timer_start(const struct device *dev)`: enable the TIM6 bus clock (`clock_control_on(clk, &cfg->pclken_tim[0])`), then:

```c
uint32_t tim_clk;
clock_control_get_rate(clk, (clock_control_subsys_t)&cfg->pclken_tim[1], &tim_clk);
uint32_t arr = (tim_clk / cfg->sampling_frequency) - 1U;   /* 160e6/8000 - 1 = 19999 */
LL_TIM_SetPrescaler(cfg->tim, 0);
LL_TIM_SetAutoReload(cfg->tim, arr);
LL_TIM_SetTriggerOutput(cfg->tim, LL_TIM_TRGO_UPDATE);
LL_TIM_GenerateEvent_UPDATE(cfg->tim);
LL_TIM_EnableCounter(cfg->tim);
```

Verify: temporarily log `arr` in `init` and confirm it prints `19999` on hardware (`west build ... && west flash ...`, read `/dev/ttyACM0`). This proves the clock-rate derivation before wiring the ADC. (TRGO itself isn't separately observable without a scope; correctness is confirmed transitively by the sample rate in Stage C.)

- [ ] **Step 3: Stage B — ADC1 LL config (channel, resolution, TIM6 trigger, DMA mode) + enable/calibrate**

Add `static int aai_adc_setup(const struct device *dev)` mirroring the STM32U5 LL ADC enable sequence (see `adc_stm32.c` for the calibration/enable pattern):

```c
ADC_TypeDef *adc = cfg->adc;
clock_control_on(clk, (clock_control_subsys_t)&cfg->pclken_adc);
/* Exit deep-power-down + enable internal regulator, wait t_ADCVREG. */
LL_ADC_DisableDeepPowerDown(adc);
LL_ADC_EnableInternalRegulator(adc);
k_busy_wait(LL_ADC_DELAY_INTERNAL_REGUL_STAB_US);
/* Calibrate (single-ended). */
LL_ADC_StartCalibration(adc, LL_ADC_CALIB_OFFSET_LINEARITY, LL_ADC_SINGLE_ENDED);
while (LL_ADC_IsCalibrationOnGoing(adc)) { }
/* Channel + resolution + 1-conversion regular sequence on channel 5. */
LL_ADC_SetResolution(adc, LL_ADC_RESOLUTION_12B);
LL_ADC_REG_SetSequencerLength(adc, LL_ADC_REG_SEQ_SCAN_DISABLE);
LL_ADC_REG_SetSequencerRanks(adc, LL_ADC_REG_RANK_1, LL_ADC_CHANNEL_5);
LL_ADC_SetChannelSamplingTime(adc, LL_ADC_CHANNEL_5, LL_ADC_SAMPLINGTIME_47CYCLES_5);
/* TIM6 TRGO trigger, rising edge. */
LL_ADC_REG_SetTriggerSource(adc, LL_ADC_REG_TRIG_EXT_TIM6_TRGO);
LL_ADC_REG_SetTriggerEdge(adc, LL_ADC_REG_TRIG_EXT_RISING);
/* One DMA request per conversion. */
LL_ADC_REG_SetDataTransferMode(adc, LL_ADC_REG_DMA_TRANSFER_LIMITED);
LL_ADC_REG_SetOverrun(adc, LL_ADC_REG_OVR_DATA_OVERWRITTEN);
/* Enable ADC, wait ADRDY. */
LL_ADC_Enable(adc);
while (!LL_ADC_IsActiveFlag_ADRDY(adc)) { }
```

**Verify-on-HW notes:** the exact sampling-time constant, calibration flags, and regulator delay may need adjustment for STM32U5 — confirm the ADC reaches `ADRDY` on hardware (add a `LOG_INF("ADC ready")` after the wait). Channel 5 sampling time 47.5 cycles is a safe default for audio-band; tune if needed.

- [ ] **Step 4: Stage C — circular DMA via Zephyr `dma_stm32u5` + start conversions**

Configure the DMA to move ADC1 → `dma_buf` in circular mode, then start the ADC regular conversions (the TRGO then paces them):

```c
data->blk = (struct dma_block_config){0};
data->blk.source_address = LL_ADC_DMA_GetRegAddr(adc, LL_ADC_DMA_REG_REGULAR_DATA);
data->blk.dest_address = (uint32_t)(uintptr_t)data->dma_buf;
data->blk.block_size = sizeof(int16_t) * 2U * cfg->block_samples;   /* full circular buffer */
data->blk.source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
data->blk.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;
data->blk.source_reload_en = 1;    /* GPDMA "emulated circular" */

data->dma_cfg = (struct dma_config){0};
data->dma_cfg.dma_slot = cfg->dma_slot;             /* LL_GPDMA1_REQUEST_ADC1 == 0 */
data->dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
data->dma_cfg.source_data_size = 2;   /* 16-bit ADC data register */
data->dma_cfg.dest_data_size = 2;
data->dma_cfg.source_burst_length = 1;
data->dma_cfg.dest_burst_length = 1;
data->dma_cfg.block_count = 1;
data->dma_cfg.head_block = &data->blk;
data->dma_cfg.dma_callback = aai_dma_cb;
data->dma_cfg.user_data = (void *)dev;

dma_config(cfg->dma_dev, cfg->dma_channel, &data->dma_cfg);
dma_start(cfg->dma_dev, cfg->dma_channel);
LL_ADC_REG_StartConversion(adc);
```

The DMA callback delivers half and full blocks:

```c
static void aai_dma_cb(const struct device *dma_dev, void *user, uint32_t channel, int status) {
  const struct device *dev = user;
  const struct aai_config *cfg = dev->config;
  struct aai_data *data = dev->data;

  if (!data->running || data->cb == NULL) {
    return;
  }
  /* DMA_STATUS_BLOCK = half-transfer (first half ready); COMPLETE = second half. */
  const int16_t *src = (status == DMA_STATUS_BLOCK) ? &data->dma_buf[0] : &data->dma_buf[cfg->block_samples];
  for (uint16_t i = 0; i < cfg->block_samples; i++) {
    data->pcm_buf[i] = adc_to_pcm16((uint16_t)src[i], cfg->resolution);
  }
  data->cb(data->pcm_buf, cfg->block_samples, data->user_data);
}
```

Wire `aai_timer_start`/`aai_adc_setup`/DMA-start into `analog_audio_in_start` (order: ADC setup → DMA config+start → timer start → ADC start conversion), and `analog_audio_in_stop` tears down (`LL_ADC_REG_StopConversion`, `dma_stop`, `LL_TIM_DisableCounter`).

- [ ] **Step 5: Add temporary rate instrumentation + a self-test start hook**

So the driver can be exercised before Task 4's SA818 integration, add a temporary test path: in `aai_init`, if `CONFIG_ANALOG_AUDIO_IN` and a debug Kconfig `ANALOG_AUDIO_IN_SELFTEST` is set, register an internal callback that counts samples and `LOG_INF`s the per-second total once it reaches `sampling_frequency`. (Or drive it from a shell command.) This isolates Task 3 from Task 4.

Flash and read `/dev/ttyACM0`:

```
source ~/zephyr-oe5xrx/bin/activate
west build -b fm_board/stm32u575xx app --build-dir build_fm && west flash --build-dir build_fm --runner pyocd
```

Expected on HW: the counter reports **~8000 samples/s** (not ~2500). If it reports 0 → the DMA/trigger isn't firing (check ADRDY, TRGO routing, DMA slot); if it reports a wrong rate → check `arr`/timer clock. Iterate here until the rate is 8000 ± a few.

- [ ] **Step 6: Commit (with the self-test hook still in, removed in Task 5)**

```bash
clang-format-18 -i drivers/audio/analog_audio_in/analog_audio_in.c
cd /home/pbuchegger/oe5xrx/FW-RemoteStation
git add drivers/audio/analog_audio_in/analog_audio_in.c drivers/audio/analog_audio_in/Kconfig
git commit -m "feat(audio): TIM6-triggered ADC1 circular-DMA capture at 8kHz (HW bring-up)"
```

---

## Task 4: Consume the module from the SA818 RX path

**Files:**
- Modify: `drivers/radio/sa818/sa818_audio_stream.cpp`, `drivers/radio/sa818/sa818_priv.h`

**Interfaces:**
- Consumes (Task 2/3): `analog_audio_in_start/stop`, `analog_audio_in_cb`. The module device is `DEVICE_DT_GET(DT_NODELABEL(audio_in))`.

- [ ] **Step 1: Replace the per-sample RX acquisition with the module callback**

In `sa818_audio_stream.cpp`: remove the RX branch of `audio_stream_work_handler` (the `adc_read_dt` block, current lines ~117–146). Add an `on_samples` callback that pushes converted PCM into the same path the old `rx_data` callback fed:

```cpp
static void aai_on_samples(const int16_t *samples, size_t count, void *user_data) {
  struct sa818_audio_stream_ctx *ctx = static_cast<sa818_audio_stream_ctx *>(user_data);
  if (!ctx->callbacks.rx_data) {
    return;
  }
  ctx->callbacks.rx_data(ctx->dev, reinterpret_cast<const uint8_t *>(samples),
                         count * sizeof(int16_t), ctx->callbacks.user_data);
}
```

- [ ] **Step 2: Start/stop the module from `sa818_audio_enable_path` (RX)**

Where RX is enabled/disabled (the `audio_rx_enabled` transition), call `analog_audio_in_start(aai_dev, aai_on_samples, &audio_ctx)` on enable and `analog_audio_in_stop(aai_dev)` on disable. Resolve `aai_dev` via `DEVICE_DT_GET(DT_NODELABEL(audio_in))` guarded by `DT_NODE_EXISTS`. The TX path (DAC) and its existing `k_work` are left untouched.

- [ ] **Step 3: Drop the now-dead ADC members / includes if unused**

If `cfg->audio_in` (`struct adc_dt_spec`) is no longer referenced anywhere in the SA818 driver after removing the RX read (check `sa818_audio.cpp` too — its one-shot monitor read of `audio_in`), remove it from `sa818_priv.h` and the DT `io-channels` binding accordingly. If `sa818_audio.cpp` still uses it for a level monitor, leave the member but note that adc1 is now owned by the capture driver — the monitor read must be removed or it will conflict. **Resolve this conflict:** the capture driver owns ADC1; any remaining `adc_read_dt(&cfg->audio_in, ...)` in the SA818 driver must be deleted.

- [ ] **Step 4: Build + on-hardware capture test**

```
source ~/zephyr-oe5xrx/bin/activate
west build -b fm_board/stm32u575xx app --build-dir build_fm && west flash --build-dir build_fm --runner pyocd
CARD=$(arecord -l | grep -i transceiver | grep -oE 'card [0-9]+|Karte [0-9]+' | grep -oE '[0-9]+' | head -1)
pasuspender -- arecord -D hw:${CARD},0 -f S16_LE -c 1 -r 8000 -d 4 /tmp/rx.wav
python3 -c "import wave,array;w=wave.open('/tmp/rx.wav');a=array.array('h');a.frombytes(w.readframes(w.getnframes()));print('frames',len(a),'peak',max(abs(x) for x in a) if a else 0)"
```

Expected: `arecord` runs its full 4 s **without `EIO`**, and the WAV has real frames with a non-zero, varying peak (feed the SA818 RX audio / open squelch to get signal). This is the end-to-end success criterion.

- [ ] **Step 5: Format and commit**

```bash
clang-format-18 -i drivers/radio/sa818/sa818_audio_stream.cpp drivers/radio/sa818/sa818_priv.h
cd /home/pbuchegger/oe5xrx/FW-RemoteStation
git add drivers/radio/sa818/sa818_audio_stream.cpp drivers/radio/sa818/sa818_priv.h drivers/radio/sa818/sa818_audio.cpp boards/oe5xrx/fm_board/fm_board.dts
git commit -m "feat(audio): drive SA818 RX capture from the hardware-timed module"
```

---

## Task 5: Remove diagnostics, docs, and final acceptance

**Files:**
- Modify: `drivers/audio/analog_audio_in/analog_audio_in.c` (remove selftest/rate instrumentation), `app/USB_AUDIO_BRIDGE.md` (or a new `drivers/audio/analog_audio_in/README.md`).

- [ ] **Step 1: Remove the temporary rate instrumentation + selftest hook** from `analog_audio_in.c` (and its Kconfig symbol). Keep only production logging.

- [ ] **Step 2: Document** the module: what it does, the DT properties, that it owns ADC1, and that TX still uses the legacy path. Note the HW acceptance results (measured 8 kHz, non-silent capture).

- [ ] **Step 3: Full verification**

```
west twister -T tests/unit_audio -p native_sim/native/64 -v --inline-logs      # adc_pcm + feedback green
west twister -T tests/usb_audio -p fm_board/stm32u575xx --build-only -v         # fm_board still builds
west build -b native_sim/native/64 app -p always                                # native_sim app builds
```

Then re-run the Task 4 on-hardware capture test and confirm: rate 8 kHz, `arecord` sustains without `EIO`, non-silent audio, and **re-evaluate the iso-IN-completion behaviour** now that the stream is fed at a true 8 kHz (per the spec's open item). Record the results in the commit / PR.

- [ ] **Step 4: Format and commit**

```bash
clang-format-18 -i drivers/audio/analog_audio_in/analog_audio_in.c
cd /home/pbuchegger/oe5xrx/FW-RemoteStation
git add -A
git commit -m "chore(audio): remove capture diagnostics, document analog-audio-in"
```

---

## Self-Review

**Spec coverage:**
- New `oe5xrx,analog-audio-in` module (driver + DT binding + API): Tasks 2–3. ✓
- TIM6 TRGO → ADC1 → circular DMA at 8 kHz via LL + Zephyr `dma_stm32u5`: Task 3. ✓ (refines the spec's "STM32 HAL" to LL+Zephyr-DMA — a cleaner path found in research; noted to the user.)
- Batch callback delivering PCM: Task 2 (interface) + Task 3 (impl). ✓
- Pure `adc_to_pcm16` + native_sim ztest in `tests/unit_audio`: Task 1. ✓
- ADC1 ownership moved off SA818; RX path consumes the module; TX untouched: Task 4. ✓
- native_sim unaffected (module STM32-only, not instantiated): Task 2 DT is fm_board-only; Task 5 verifies native_sim app + tests. ✓
- HW acceptance (8 kHz, non-silent arecord, sustained, re-check iso-IN): Tasks 4–5. ✓
- Non-goals (no TX, no I2S, no sim backend): respected — TX explicitly untouched. ✓

**Placeholder scan:** No TBD/TODO. The inline "verify on HW" notes on Task 3 are deliberate bring-up markers on hardware register values that cannot be statically guaranteed, not skipped work; each is paired with a concrete starting value and an observable check.

**Type consistency:** `analog_audio_in_cb`, `analog_audio_in_start`, `analog_audio_in_stop`, and `adc_to_pcm16(uint16_t,uint8_t)->int16_t` are used identically across Tasks 1–4. `block_samples`/`resolution`/`sampling_frequency` names match between the DT binding, config struct, and usage.
