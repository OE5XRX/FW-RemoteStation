# Hardware-Timed Analog Audio Playback (TX) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Output the SA818 TX audio (host → DAC → SA818 mic-in) at a true, hardware-clocked 8 kHz (STM32 TIM7 TRGO → DAC1 CH1 → memory→DAC circular DMA), replacing the broken per-sample `k_work` DAC loop, so transmitted audio is correct and the PR #56 feedback regulator works against a real 8 kHz sink.

**Architecture:** A new `oe5xrx,analog-audio-out` driver — the DAC mirror of `oe5xrx,analog-audio-in`: TIM7 paces DAC1 CH1 conversions fed by a circular memory→DAC DMA; a half/full-transfer ISR hands off to the system workqueue, which **pulls** PCM from a source callback (→ the bridge TX ring), converts PCM→DAC codes, and refills the just-consumed buffer half (padding underflow with DAC mid-scale = silence). The SA818 TX path consumes it instead of the per-sample `dac_write_value`.

**Tech Stack:** Zephyr RTOS (v4.4.99), STM32U575, STM32 LL (DAC + TIM), Zephyr `dma_stm32u5`, devicetree, C driver layer, ztest (native_sim).

## Nature of this plan

Tasks 1–2 are deterministic (pure helper + scaffolding). **Tasks 3–4 are STM32 hardware bring-up** — concrete LL/DMA code from the STM32U5 patterns, but peripheral bring-up needs on-hardware iteration; each hardware task ends in an on-board measurement. Reuse the RX-proven debug method: `volatile` counters read via `pyocd commander -c halt -c "read32 <nm-addr>"` (USB-CDC console reads are unreliable across re-enumeration). The `drivers/audio/analog_audio_in/analog_audio_in.c` driver is the reference; the DAC mirror swaps source/dest roles.

Build/flash: `west` in venv `~/zephyr-oe5xrx`; `west build -b fm_board/stm32u575xx app --build-dir build_fm`; `west flash --build-dir build_fm --runner pyocd`; console `/dev/ttyACM0` @115200.

## Global Constraints

- Driver-layer C idiom: `DT_DRV_COMPAT`, `DEVICE_DT_INST_DEFINE`, `extern "C"` C-ABI header, no dynamic alloc/exceptions/RTTI.
- 8 kHz, 16-bit PCM, mono. DAC is 12-bit, PA4, DAC1 channel 1.
- clang-format-18 clean on all `.c/.h/.cpp` under `app/`, `boards/`, `tests/`, `drivers/`.
- Test dirs only: `tests/etl`, `tests/sim_shell`, `tests/usb_audio`, `tests/unit_audio`.
- Scope: DAC audio path only. **PTT is NOT keyed by this module** (stays manual/agent via `sa818_set_ptt`). RX unchanged.
- Known HW values (STM32U575 @ 160 MHz APB1 timer clock; verify TRGO period on HW):
  - DAC trigger: `LL_DAC_SetTriggerSource(DAC1, LL_DAC_CHANNEL_1, LL_DAC_TRIG_EXT_TIM7_TRGO)` — **set while the channel is disabled**.
  - DAC output: `LL_DAC_ConfigOutput(DAC1, LL_DAC_CHANNEL_1, LL_DAC_OUTPUT_MODE_NORMAL, LL_DAC_OUTPUT_BUFFER_ENABLE, LL_DAC_OUTPUT_CONNECT_GPIO)`.
  - DAC enable order: ConfigOutput → SetTriggerSource → `LL_DAC_EnableDMAReq` → `LL_DAC_EnableTrigger` → `LL_DAC_Enable` → `k_busy_wait(LL_DAC_DELAY_STARTUP_VOLTAGE_SETTLING_US)` (8 µs).
  - DMA dest reg: `LL_DAC_DMA_GetRegAddr(DAC1, LL_DAC_CHANNEL_1, LL_DAC_DMA_REG_DATA_12BITS_RIGHT_ALIGNED)`.
  - GPDMA request slot: `LL_GPDMA1_REQUEST_DAC1_CH1` == `2`. Use GPDMA **channel 1** (the RX/ADC path uses channel 0).
  - Circular memory→DAC via `dma_stm32u5`: `channel_direction = MEMORY_TO_PERIPHERAL`, `head_block.source_reload_en = 1` (the field that arms cyclic mode in this driver, regardless of direction — do NOT use `dest_reload_en`, it is dead code here), `source_addr_adj = DMA_ADDR_ADJ_INCREMENT` (memory buffer), `dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE` (DAC reg). Half-transfer → `DMA_STATUS_BLOCK`, full → `DMA_STATUS_COMPLETE`.
  - `dmas` DT cell: `<&gpdma1 1 2 (STM32_DMA_16BITS | STM32_DMA_PRIORITY_HIGH)>` (channel 1, slot 2).
  - TIM7: `&timers7` (compatible `st,stm32-timers`, `clocks = <&rcc STM32_CLOCK(APB1,5)>, <&rcc STM32_SRC_TIMPCLK1 NO_SEL>`) is `status="disabled"` and NOT yet in `fm_board.dts` — add it (`status="okay"; st,mastermode="UPDATE"; st,prescaler=<0>;`). Same PSC=0/ARR=19999 runtime derivation as TIM6.
  - DAC clock: only the AHB3 bus clock via `clock_control_on(clk, &cfg->dac_pclken)` (single clock cell; NO kernel-clock `clock_control_configure` step, unlike the ADC).
  - DAC channel from DT: `DT_INST_IO_CHANNELS_OUTPUT(inst)` (the "output" cell of `io-channels = <&dac1 1>`); LL channel via `__LL_DAC_DECIMAL_NB_TO_CHANNEL` if available, else map (1→`LL_DAC_CHANNEL_1`).
  - `&dac1` is already enabled on fm_board (PA4, `dac1_out1_pa4`). `&gpdma1` already enabled by the RX work. `CONFIG_DAC=y`, `CONFIG_DMA=y` already set.

---

## File Structure

**Create:**
- `drivers/audio/analog_audio_out/dac_pcm.h` / `dac_pcm.c` — pure `pcm16_to_dac`.
- `drivers/audio/analog_audio_out/analog_audio_out.c` — the driver.
- `drivers/audio/analog_audio_out/Kconfig`, `CMakeLists.txt`, `README.md`.
- `include/oe5xrx/audio/analog_audio_out.h` — public C ABI.
- `dts/bindings/audio/oe5xrx,analog-audio-out.yaml`.

**Modify:**
- `drivers/audio/Kconfig` (`rsource "analog_audio_out/Kconfig"`), `drivers/audio/CMakeLists.txt` (`add_subdirectory_ifdef(CONFIG_ANALOG_AUDIO_OUT analog_audio_out)`).
- `boards/oe5xrx/fm_board/fm_board.dts` — enable `&timers7`; add the `analog-audio-out` node.
- `boards/oe5xrx/fm_board/fm_board_defconfig` — `CONFIG_ANALOG_AUDIO_OUT=y`.
- `drivers/radio/sa818/sa818_audio_stream.cpp` — TX consumes the module; delete the DAC `k_work` loop (and the work handler if fully unused).
- `tests/unit_audio/src/main.cpp` + `CMakeLists.txt` — `pcm16_to_dac` ztest.

---

## Task 1: `pcm16_to_dac` pure helper + native_sim ztest

**Files:** Create `drivers/audio/analog_audio_out/dac_pcm.h`, `dac_pcm.c`; Modify `tests/unit_audio/src/main.cpp`, `tests/unit_audio/CMakeLists.txt`

**Interfaces:**
- Produces: `uint16_t pcm16_to_dac(int16_t sample, uint8_t resolution);` — signed 16-bit PCM → unsigned DAC code: `(sample + 32768)` right-shifted by `16 - resolution` (clamped to [1,16]); mid-scale (silence) for `sample == 0`.

- [ ] **Step 1: Header** — `drivers/audio/analog_audio_out/dac_pcm.h`:

```c
/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef OE5XRX_ANALOG_AUDIO_OUT_DAC_PCM_H_
#define OE5XRX_ANALOG_AUDIO_OUT_DAC_PCM_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Convert signed 16-bit PCM to an unsigned DAC code of @p resolution bits.
 * Offsets by the midpoint so PCM 0 maps to DAC mid-scale (silence), then scales
 * down to the DAC resolution. @p resolution is clamped to [1, 16].
 */
uint16_t pcm16_to_dac(int16_t sample, uint8_t resolution);

#ifdef __cplusplus
}
#endif

#endif /* OE5XRX_ANALOG_AUDIO_OUT_DAC_PCM_H_ */
```

- [ ] **Step 2: Failing tests** — add to `tests/unit_audio/src/main.cpp`:

```cpp
#include "dac_pcm.h"

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
```

Add the source + include dir to `tests/unit_audio/CMakeLists.txt`:

```cmake
target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../drivers/audio/analog_audio_out)

target_sources(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src/main.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/feedback.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../drivers/audio/analog_audio_in/adc_pcm.c
  ${CMAKE_CURRENT_LIST_DIR}/../../drivers/audio/analog_audio_out/dac_pcm.c
)
```

- [ ] **Step 3: Run — expect FAIL** (undefined `pcm16_to_dac`).

Run: `west twister -T tests/unit_audio -p native_sim/native/64 -v --inline-logs`

- [ ] **Step 4: Implement** — `drivers/audio/analog_audio_out/dac_pcm.c`:

```c
/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "dac_pcm.h"

uint16_t pcm16_to_dac(int16_t sample, uint8_t resolution) {
  if (resolution < 1) {
    resolution = 1;
  } else if (resolution > 16) {
    resolution = 16;
  }
  uint32_t unsigned_sample = (uint32_t)((int32_t)sample + 32768);
  uint8_t down_shift = (uint8_t)(16 - resolution);
  return (uint16_t)(unsigned_sample >> down_shift);
}
```

- [ ] **Step 5: Run — expect PASS** (dac_pcm 3/3 + adc_pcm + feedback all green).

- [ ] **Step 6: Format + commit:**

```bash
clang-format-18 -i drivers/audio/analog_audio_out/dac_pcm.h drivers/audio/analog_audio_out/dac_pcm.c tests/unit_audio/src/main.cpp
git add drivers/audio/analog_audio_out/dac_pcm.h drivers/audio/analog_audio_out/dac_pcm.c tests/unit_audio
git commit -m "feat(audio): add pcm16_to_dac helper + native_sim tests"
```

---

## Task 2: Driver scaffolding, DT binding, build wiring (no hardware yet)

**Files:** Create `include/oe5xrx/audio/analog_audio_out.h`, `drivers/audio/analog_audio_out/{analog_audio_out.c,Kconfig,CMakeLists.txt}`, `dts/bindings/audio/oe5xrx,analog-audio-out.yaml`; Modify `drivers/audio/Kconfig`, `drivers/audio/CMakeLists.txt`, `boards/oe5xrx/fm_board/fm_board.dts`, `boards/oe5xrx/fm_board/fm_board_defconfig`

**Interfaces (consumed by Tasks 3–4):**
- `typedef size_t (*analog_audio_out_src)(int16_t *dst, size_t max, void *user_data);`
- `int analog_audio_out_start(const struct device *dev, analog_audio_out_src src, void *user_data);`
- `int analog_audio_out_stop(const struct device *dev);`

- [ ] **Step 1: Public header** — `include/oe5xrx/audio/analog_audio_out.h`:

```c
/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef OE5XRX_AUDIO_ANALOG_AUDIO_OUT_H_
#define OE5XRX_AUDIO_ANALOG_AUDIO_OUT_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Fill up to @p max PCM samples into @p dst; return the count provided (0..max).
 *  Runs in thread context (system workqueue); may take a mutex. */
typedef size_t (*analog_audio_out_src)(int16_t *dst, size_t max, void *user_data);

/** Start hardware-timed playback; @p src is polled to refill each DMA block. */
int analog_audio_out_start(const struct device *dev, analog_audio_out_src src, void *user_data);

/** Stop playback. */
int analog_audio_out_stop(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* OE5XRX_AUDIO_ANALOG_AUDIO_OUT_H_ */
```

- [ ] **Step 2: DT binding** — `dts/bindings/audio/oe5xrx,analog-audio-out.yaml` (mirror the analog-audio-in binding, `io-channels` = DAC):

```yaml
# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

description: |
  Hardware-timed analog audio playback: a timer TRGO paces DAC conversions fed
  from a circular memory->DAC DMA. STM32-specific.

compatible: "oe5xrx,analog-audio-out"

include: base.yaml

properties:
  io-channels:
    required: true
    description: DAC instance + channel used for the audio output.
  sampling-timer:
    type: phandle
    required: true
  dmas:
    required: true
  dma-names:
    required: true
  sampling-frequency:
    type: int
    required: true
  resolution:
    type: int
    required: true
  block-samples:
    type: int
    required: true
```

- [ ] **Step 3: Driver skeleton** — `drivers/audio/analog_audio_out/analog_audio_out.c` (config parsed; start/stop log only; Task 3 fills the hardware):

```c
/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#define DT_DRV_COMPAT oe5xrx_analog_audio_out

#include <oe5xrx/audio/analog_audio_out.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(analog_audio_out, CONFIG_ANALOG_AUDIO_OUT_LOG_LEVEL);

struct aao_config {
  uint32_t sampling_frequency;
  uint16_t block_samples;
  uint8_t resolution;
};

struct aao_data {
  analog_audio_out_src src;
  void *user_data;
  bool running;
};

int analog_audio_out_start(const struct device *dev, analog_audio_out_src src, void *user_data) {
  struct aao_data *data = dev->data;

  if (src == NULL) {
    return -EINVAL;
  }
  data->src = src;
  data->user_data = user_data;
  data->running = true;
  LOG_INF("start (hardware path added in a later step)");
  return 0;
}

int analog_audio_out_stop(const struct device *dev) {
  struct aao_data *data = dev->data;

  data->running = false;
  return 0;
}

static int aao_init(const struct device *dev) {
  const struct aao_config *cfg = dev->config;

  LOG_INF("init: %u Hz, %u-bit, block=%u", cfg->sampling_frequency, cfg->resolution, cfg->block_samples);
  return 0;
}

#define AAO_INIT(inst)                                                                                                                                         \
  static const struct aao_config aao_cfg_##inst = {                                                                                                            \
      .sampling_frequency = DT_INST_PROP(inst, sampling_frequency),                                                                                             \
      .block_samples = DT_INST_PROP(inst, block_samples),                                                                                                       \
      .resolution = DT_INST_PROP(inst, resolution),                                                                                                            \
  };                                                                                                                                                           \
  static struct aao_data aao_data_##inst;                                                                                                                      \
  DEVICE_DT_INST_DEFINE(inst, aao_init, NULL, &aao_data_##inst, &aao_cfg_##inst, POST_KERNEL, CONFIG_ANALOG_AUDIO_OUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(AAO_INIT)
```

- [ ] **Step 4: Kconfig + CMake** (mirror `drivers/audio/analog_audio_in`):

`drivers/audio/analog_audio_out/Kconfig`:

```
# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

config ANALOG_AUDIO_OUT
	bool "Hardware-timed analog audio playback (TIM+DAC+DMA)"
	default y
	depends on DT_HAS_OE5XRX_ANALOG_AUDIO_OUT_ENABLED
	select DMA
	help
	  Timer-TRGO-triggered DAC + circular DMA analog audio playback driver.

if ANALOG_AUDIO_OUT
config ANALOG_AUDIO_OUT_INIT_PRIORITY
	int "Init priority"
	default 90
	help
	  Device init priority. Must be after the DMA and DAC drivers.
module = ANALOG_AUDIO_OUT
module-str = analog_audio_out
source "subsys/logging/Kconfig.template.log_config"
endif
```

`drivers/audio/analog_audio_out/CMakeLists.txt`:

```cmake
# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later
zephyr_library()
zephyr_library_sources(analog_audio_out.c dac_pcm.c)
zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../include)
```

In `drivers/audio/Kconfig`, add `rsource "analog_audio_out/Kconfig"`. In `drivers/audio/CMakeLists.txt`, add `add_subdirectory_ifdef(CONFIG_ANALOG_AUDIO_OUT analog_audio_out)`.

- [ ] **Step 5: Devicetree** — in `boards/oe5xrx/fm_board/fm_board.dts`, enable TIM7 (mirror the `&timers6` block) and add the node:

```dts
&timers7 {
	status = "okay";
	st,mastermode = "UPDATE";
	st,prescaler = <0>;
};
```

```dts
	audio_out: analog-audio-out {
		compatible = "oe5xrx,analog-audio-out";
		io-channels = <&dac1 1>;
		io-channel-names = "audio_out";
		sampling-timer = <&timers7>;
		dmas = <&gpdma1 1 2 (STM32_DMA_16BITS | STM32_DMA_PRIORITY_HIGH)>;
		dma-names = "tx";
		sampling-frequency = <8000>;
		resolution = <12>;
		block-samples = <8>;
	};
```

In `boards/oe5xrx/fm_board/fm_board_defconfig`, add `CONFIG_ANALOG_AUDIO_OUT=y`.

- [ ] **Step 6: Build-verify fm_board:** `west build -b fm_board/stm32u575xx app --build-dir build_fm -p always` — clean; boot log shows `analog_audio_out: init: 8000 Hz, 12-bit, block=8`. Resolve any DT error (the `&dac1 1` io-channel is also referenced by the `sa818` node — this dual reference is inert at this task's scope, same as the ADC in the RX work).

- [ ] **Step 7: Format + commit:**

```bash
clang-format-18 -i drivers/audio/analog_audio_out/analog_audio_out.c include/oe5xrx/audio/analog_audio_out.h
git add drivers/audio/analog_audio_out include/oe5xrx/audio/analog_audio_out.h dts/bindings/audio/oe5xrx,analog-audio-out.yaml drivers/audio/Kconfig drivers/audio/CMakeLists.txt boards/oe5xrx/fm_board/fm_board.dts boards/oe5xrx/fm_board/fm_board_defconfig
git commit -m "feat(audio): scaffold oe5xrx,analog-audio-out driver + DT wiring"
```

---

## Task 3: Hardware bring-up — TIM7 → DAC1 → circular DMA + refill hand-off

**Files:** Modify `drivers/audio/analog_audio_out/analog_audio_out.c`

**Interfaces:** Consumes Task 1 (`pcm16_to_dac`) + Task 2 (skeleton, `analog_audio_out_src`). Produces a working `analog_audio_out_start/stop` that outputs 8 kHz from the `src` callback.

Build in stages, flashing and reading a `volatile` refill counter via pyocd after each. Add a Kconfig-gated self-test (a sine `src`) so the module can be exercised before Task 4.

- [ ] **Step 1: config/data + includes.** Add to config (parsed via the macro): `TIM_TypeDef *tim`, `DAC_TypeDef *dac`, `uint32_t dac_ll_channel`, the TIM `stm32_pclken` array and the DAC `stm32_pclken` (single entry), the DMA `dev`/`channel`/`slot`. Add to data: `uint16_t dma_buf[2 * AAO_MAX_BLOCK]` (DAC codes), `struct dma_config`/`struct dma_block_config`, a `struct k_work refill_work`, `const struct device *self`, and (self-test) a `volatile uint32_t dbg_refills`.

Includes: `<stm32_ll_dac.h>`, `<stm32_ll_tim.h>`, `<zephyr/drivers/clock_control.h>`, `<zephyr/drivers/clock_control/stm32_clock_control.h>`, `<zephyr/drivers/dma.h>`, `"dac_pcm.h"`. Config macro accessors:
- `.dac = (DAC_TypeDef *)DT_REG_ADDR(DT_INST_IO_CHANNELS_CTLR(inst))`
- `.dac_ll_channel = LL_DAC_CHANNEL_1` (or `__LL_DAC_DECIMAL_NB_TO_CHANNEL(DT_INST_IO_CHANNELS_OUTPUT(inst))` if the macro exists)
- `.tim = (TIM_TypeDef *)DT_REG_ADDR(DT_INST_PHANDLE(inst, sampling_timer))`
- `.tim_pclken = STM32_DT_CLOCKS(DT_INST_PHANDLE(inst, sampling_timer))` (array)
- `.dac_pclken = STM32_DT_CLOCKS(DT_INST_IO_CHANNELS_CTLR(inst))[0]` — a single entry; store as `struct stm32_pclken` (the DAC has one clock cell). Use an array of size 1 and index `[0]`.
- `.dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(inst, tx))`, `.dma_channel = DT_INST_DMAS_CELL_BY_NAME(inst, tx, channel)`, `.dma_slot = DT_INST_DMAS_CELL_BY_NAME(inst, tx, slot)`.

- [ ] **Step 2: DAC setup** — `aao_dac_setup(dev)`:

```c
DAC_TypeDef *dac = cfg->dac;
const struct device *clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
(void)clock_control_on(clk, (clock_control_subsys_t)&cfg->dac_pclken); /* AHB3 bus only */

/* Output stage + trigger must be configured while the channel is disabled. */
LL_DAC_ConfigOutput(dac, cfg->dac_ll_channel, LL_DAC_OUTPUT_MODE_NORMAL, LL_DAC_OUTPUT_BUFFER_ENABLE, LL_DAC_OUTPUT_CONNECT_GPIO);
LL_DAC_SetTriggerSource(dac, cfg->dac_ll_channel, LL_DAC_TRIG_EXT_TIM7_TRGO);
LL_DAC_EnableDMAReq(dac, cfg->dac_ll_channel);
LL_DAC_EnableTrigger(dac, cfg->dac_ll_channel);
LL_DAC_Enable(dac, cfg->dac_ll_channel);
k_busy_wait(LL_DAC_DELAY_STARTUP_VOLTAGE_SETTLING_US);
```

**Verify-on-HW note:** `LL_DAC_OUTPUT_BUFFER_ENABLE` + `CONNECT_GPIO` (buffered output to PA4) is the conventional audio-out choice; adjust if the analog load needs otherwise.

- [ ] **Step 3: prefill silence + circular memory→DAC DMA** — before enabling the trigger, fill `dma_buf` with the DAC mid-scale (`pcm16_to_dac(0, resolution)`) so nothing spurious is emitted, then:

```c
data->blk = (struct dma_block_config){0};
data->blk.source_address = (uint32_t)(uintptr_t)data->dma_buf;
data->blk.dest_address = LL_DAC_DMA_GetRegAddr(cfg->dac, cfg->dac_ll_channel, LL_DAC_DMA_REG_DATA_12BITS_RIGHT_ALIGNED);
data->blk.block_size = sizeof(uint16_t) * 2U * cfg->block_samples;
data->blk.source_addr_adj = DMA_ADDR_ADJ_INCREMENT;   /* walk the memory buffer */
data->blk.dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;     /* fixed DAC data register */
data->blk.source_reload_en = 1;                       /* arms cyclic mode (this driver) */

data->dma_cfg = (struct dma_config){0};
data->dma_cfg.dma_slot = cfg->dma_slot;               /* LL_GPDMA1_REQUEST_DAC1_CH1 == 2 */
data->dma_cfg.channel_direction = MEMORY_TO_PERIPHERAL;
data->dma_cfg.source_data_size = 2;
data->dma_cfg.dest_data_size = 2;
data->dma_cfg.source_burst_length = 2;
data->dma_cfg.dest_burst_length = 2;
data->dma_cfg.block_count = 1;
data->dma_cfg.head_block = &data->blk;
data->dma_cfg.dma_callback = aao_dma_cb;
data->dma_cfg.user_data = (void *)dev;

if (dma_config(cfg->dma_dev, cfg->dma_channel, &data->dma_cfg) < 0) { ... }
dma_start(cfg->dma_dev, cfg->dma_channel);
```

- [ ] **Step 4: TIM7 start** — identical to the RX `aai_timer_start` but on `cfg->tim` (TIM7): enable bus clock, `clock_control_get_rate` (check return, guard `tim_clk==0`), `LL_TIM_SetPrescaler(tim,0)`, `LL_TIM_SetAutoReload(tim, tim_clk/freq - 1)`, `LL_TIM_SetTriggerOutput(tim, LL_TIM_TRGO_UPDATE)`, `LL_TIM_GenerateEvent_UPDATE(tim)`, `LL_TIM_EnableCounter(tim)`.

- [ ] **Step 5: DMA callback + refill hand-off** — the ISR submits the refill work; the work refills the just-consumed half from `src`, padding with mid-scale:

```c
static void aao_dma_cb(const struct device *dma_dev, void *user, uint32_t channel, int status) {
  struct aao_data *data = ((const struct device *)user)->data;
  ARG_UNUSED(dma_dev); ARG_UNUSED(channel);
  if (!data->running || status < 0) { return; }
  data->half = (status == DMA_STATUS_BLOCK) ? 0 : 1;   /* which half just finished playing */
  k_work_submit(&data->refill_work);
}

static void aao_refill_work(struct k_work *work) {
  struct aao_data *data = CONTAINER_OF(work, struct aao_data, refill_work);
  const struct aao_config *cfg = data->self->config;
  int16_t pcm[AAO_MAX_BLOCK];
  uint16_t *dst = &data->dma_buf[data->half * cfg->block_samples];
  size_t got = data->src ? data->src(pcm, cfg->block_samples, data->user_data) : 0;
  for (size_t i = 0; i < cfg->block_samples; i++) {
    dst[i] = (i < got) ? pcm16_to_dac(pcm[i], cfg->resolution) : pcm16_to_dac(0, cfg->resolution);
  }
  data->dbg_refills += cfg->block_samples;   /* self-test counter */
}
```

Wire `aao_dac_setup` → `aao_dma_start` → `aao_timer_start` into `analog_audio_out_start` (with the re-entrancy guard and `running` reset on failure like the RX driver). `analog_audio_out_stop`: `running=false`, `LL_TIM_DisableCounter`, `LL_DAC_DisableTrigger`, `dma_stop`.

- [ ] **Step 6: Kconfig self-test** (`ANALOG_AUDIO_OUT_SELFTEST`) — a `src` that generates a fixed-frequency sine (integer LUT, no float), started via a 6 s delayed work (system workqueue, not shell — same reasoning as RX). It lets the DAC output a known tone and increments `dbg_refills`.

- [ ] **Step 7: HW verify.** Build with `-DCONFIG_ANALOG_AUDIO_OUT_SELFTEST=y -p always`, flash. Read `dbg_refills` twice ~1 s apart via `pyocd commander -t stm32u575citx -c halt -c "read32 <nm dbg_refills>"` — expect ~8000/s increment (steady). If a scope/second radio is available, confirm the sine on PA4 / hear the tone. Iterate on DAC/DMA config until refills are steady at 8 kHz.

- [ ] **Step 8: Format + commit** `feat(audio): TIM7-triggered DAC1 circular-DMA playback at 8kHz (HW bring-up)`.

---

## Task 4: SA818 TX integration

**Files:** Modify `drivers/radio/sa818/sa818_audio_stream.cpp` (+ `sa818_priv.h` if needed)

- [ ] **Step 1: src callback** — pull PCM from the existing `tx_request` callback (bytes → samples):

```cpp
#ifdef SA818_HAVE_AAO
static size_t sa818_aao_src(int16_t *dst, size_t max, void *user) {
  struct sa818_audio_stream_ctx *ctx = static_cast<sa818_audio_stream_ctx *>(user);
  if (!ctx->callbacks.tx_request) { return 0; }
  size_t bytes = ctx->callbacks.tx_request(ctx->dev, reinterpret_cast<uint8_t *>(dst), max * sizeof(int16_t), ctx->callbacks.user_data);
  return bytes / sizeof(int16_t);
}
#endif
```

Guard with `#if DT_NODE_EXISTS(DT_NODELABEL(audio_out))` → `#define SA818_HAVE_AAO 1` and include `<oe5xrx/audio/analog_audio_out.h>`.

- [ ] **Step 2: start/stop the module** in `sa818_audio_stream_start/stop` (surface start errors via `LOG_ERR`, like the RX integration).

- [ ] **Step 3: remove the DAC `k_work` TX loop** from `audio_stream_work_handler`. Since RX was already removed (PR #57), the handler now does nothing per-tick — **delete `audio_stream_work_handler` and its `k_work_reschedule`/`k_work_init_delayable`** if nothing else uses it. Verify the test-tone path (`test_tone_work`) is separate and untouched; verify `tx_buffer`/`rx_buffer` fields and any now-unused `cfg->audio_out*`/`cfg->audio_in` references are cleaned or intentionally kept (the DAC is now driven via LL by the module; a residual `dac_write_value` in `sa818_audio.cpp` test-tone/reset path must not run during streaming — guard or note).

- [ ] **Step 4: HW acceptance.** Build (no selftest) + flash. Play speech/tone from the laptop to the "FM Transceiver Board" playback device, `sa818 ptt on` in the shell, and confirm on a second radio (RX) that the audio is clean and correct-pitch. Confirm a sustained TX keeps the TX ring centered (feedback regulator) with no under/overrun in the log.

- [ ] **Step 5: Format + commit** `feat(audio): drive SA818 TX from the hardware-timed DAC module`.

---

## Task 5: Remove diagnostics, docs, verification

- [ ] **Step 1:** Remove the self-test + `dbg_refills` from `analog_audio_out.c` and the SELFTEST Kconfig.
- [ ] **Step 2:** Add `drivers/audio/analog_audio_out/README.md` (mirror the analog-audio-in README: TIM7→DAC→DMA design, pull/refill + underflow→silence, API, DT, coexistence + PTT-is-manual notes).
- [ ] **Step 3: Full verification:**

```bash
west twister -T tests/unit_audio -p native_sim/native/64 -v --inline-logs      # dac_pcm + adc_pcm + feedback green
west twister -T tests/usb_audio -p fm_board/stm32u575xx --build-only -v         # fm_board builds
west build -b native_sim/native/64 app -p always                                # native_sim app builds
```

Then re-run the Task 4 HW acceptance and record results in the PR.

- [ ] **Step 4: Format + commit** `chore(audio): remove playback diagnostics, document analog-audio-out`.

---

## Self-Review

**Spec coverage:** module + binding (T2–3) ✓; TIM7→DAC→circular DMA @8kHz via LL+dma_stm32u5 (T3) ✓; pull `src` + underflow→silence (T3 refill) ✓; `pcm16_to_dac` + native_sim test (T1) ✓; SA818 TX integration, delete DAC k_work / work handler (T4) ✓; feedback regulator now correct (T4 acceptance) ✓; PTT out of scope (never keyed) ✓; RX unchanged ✓.

**Placeholder scan:** the `<&gpdma1 1 2 ...>` / TIM7 ARR / `BUFFER_ENABLE` items are concrete values or explicit "verify on HW" bring-up markers, each with a starting value + observable check — not skipped work.

**Type consistency:** `analog_audio_out_src`/`analog_audio_out_start`/`_stop` and `pcm16_to_dac(int16_t,uint8_t)->uint16_t` used identically across tasks; `block_samples`/`resolution`/`sampling_frequency` names match the binding, config, and usage. DMA channel 1 / slot 2 (DAC) distinct from the RX channel 0 / slot 0.
