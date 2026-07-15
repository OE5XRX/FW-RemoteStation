/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#define DT_DRV_COMPAT oe5xrx_analog_audio_out

#include "dac_pcm.h"

#include <oe5xrx/audio/analog_audio_out.h>
#include <stm32_ll_dac.h>
#include <stm32_ll_dma.h>
#include <stm32_ll_tim.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(analog_audio_out, CONFIG_ANALOG_AUDIO_OUT_LOG_LEVEL);

/* Max samples per DMA half; the circular buffer is 2x this. */
#define AAO_MAX_BLOCK 16

struct aao_config {
  uint32_t sampling_frequency;
  uint16_t block_samples;
  uint8_t resolution;
  uint32_t dac_channel_nb; /* DT io-channels output cell (1 or 2) */
  TIM_TypeDef *tim;
  DAC_TypeDef *dac;
  const struct stm32_pclken *tim_pclken; /* [0]=bus enable, [1]=kernel clock */
  const struct stm32_pclken *dac_pclken; /* [0]=bus enable (single cell) */
  const struct device *dma_dev;
  uint32_t dma_channel;
  uint32_t dma_slot;
};

struct aao_data {
  const struct device *self;
  analog_audio_out_src src;
  void *user_data;
  atomic_t running;                    /* written from thread (start/stop), read from DMA ISR */
  uint16_t dma_buf[2 * AAO_MAX_BLOCK]; /* circular DAC codes: [0..block) | [block..2*block) */
  volatile uint8_t refill_half;        /* which half just finished playing (0/1) */
  struct k_work refill_work;
  struct dma_config dma_cfg;
  struct dma_block_config blk;
};

static uint32_t aao_ll_channel(uint32_t nb) {
  return (nb == 2) ? LL_DAC_CHANNEL_2 : LL_DAC_CHANNEL_1;
}

/* System-workqueue handler: refill the just-consumed buffer half by pulling PCM
 * from the source (thread context, may block/take a mutex), converting to DAC
 * codes, and padding any shortfall with mid-scale (silence). */
static void aao_refill_work(struct k_work *work) {
  struct aao_data *data = CONTAINER_OF(work, struct aao_data, refill_work);
  const struct aao_config *cfg = data->self->config;
  int16_t pcm[AAO_MAX_BLOCK];
  uint16_t *dst = &data->dma_buf[data->refill_half * cfg->block_samples];
  uint16_t mid = pcm16_to_dac(0, cfg->resolution);

  size_t got = data->src ? data->src(pcm, cfg->block_samples, data->user_data) : 0;
  for (uint16_t i = 0; i < cfg->block_samples; i++) {
    dst[i] = (i < got) ? pcm16_to_dac(pcm[i], cfg->resolution) : mid;
  }
}

static void aao_dma_cb(const struct device *dma_dev, void *user, uint32_t channel, int status) {
  const struct device *dev = user;
  struct aao_data *data = dev->data;

  ARG_UNUSED(dma_dev);
  ARG_UNUSED(channel);

  if (!atomic_get(&data->running)) {
    return;
  }
  /* Refill only on the expected half/full-transfer completions:
   * DMA_STATUS_BLOCK = first half just played, DMA_STATUS_COMPLETE = second half.
   * Ignore errors (status < 0) and any other/unexpected status. */
  if (status == DMA_STATUS_BLOCK) {
    data->refill_half = 0;
  } else if (status == DMA_STATUS_COMPLETE) {
    data->refill_half = 1;
  } else {
    return;
  }
  k_work_submit(&data->refill_work);
}

static int aao_dac_setup(const struct device *dev) {
  const struct aao_config *cfg = dev->config;
  const struct device *clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
  DAC_TypeDef *dac = cfg->dac;
  uint32_t ch = aao_ll_channel(cfg->dac_channel_nb);

  /* DAC needs only its bus clock (single clock cell; no kernel clock). */
  (void)clock_control_on(clk, (clock_control_subsys_t)&cfg->dac_pclken[0]);

  /* STM32U5: the DAC digital interface must be told the AHB clock range via HFSEL.
   * At the default reset value (<=80 MHz) the interface cannot keep up at higher
   * HCLK. Set it from the actual DAC bus-clock rate while the channel is disabled. */
  uint32_t dac_bus_hz = 0;
  (void)clock_control_get_rate(clk, (clock_control_subsys_t)&cfg->dac_pclken[0], &dac_bus_hz);
  uint32_t hfsel = LL_DAC_HIGH_FREQ_MODE_DISABLE;
  if (dac_bus_hz > 160000000U) {
    hfsel = LL_DAC_HIGH_FREQ_MODE_ABOVE_160MHZ;
  } else if (dac_bus_hz > 80000000U) {
    hfsel = LL_DAC_HIGH_FREQ_MODE_ABOVE_80MHZ;
  }
  LL_DAC_SetHighFrequencyMode(dac, hfsel);

  /* Output stage + trigger source must be set while the channel is disabled. */
  LL_DAC_ConfigOutput(dac, ch, LL_DAC_OUTPUT_MODE_NORMAL, LL_DAC_OUTPUT_BUFFER_ENABLE, LL_DAC_OUTPUT_CONNECT_GPIO);
  LL_DAC_SetTriggerSource(dac, ch, LL_DAC_TRIG_EXT_TIM7_TRGO);
  if (ch == LL_DAC_CHANNEL_1) {
    LL_DAC_ClearFlag_DMAUDR1(dac);
  } else {
    LL_DAC_ClearFlag_DMAUDR2(dac);
  }
  LL_DAC_EnableDMAReq(dac, ch);
  LL_DAC_EnableTrigger(dac, ch);
  LL_DAC_Enable(dac, ch);
  k_busy_wait(LL_DAC_DELAY_STARTUP_VOLTAGE_SETTLING_US);
  return 0;
}

static int aao_dma_start(const struct device *dev) {
  const struct aao_config *cfg = dev->config;
  struct aao_data *data = dev->data;
  uint16_t mid = pcm16_to_dac(0, cfg->resolution);

  /* Prefill the whole circular buffer with silence before the trigger fires. */
  for (uint16_t i = 0; i < 2U * cfg->block_samples; i++) {
    data->dma_buf[i] = mid;
  }

  data->blk = (struct dma_block_config){0};
  data->blk.source_address = (uint32_t)(uintptr_t)data->dma_buf;
  data->blk.dest_address = LL_DAC_DMA_GetRegAddr(cfg->dac, aao_ll_channel(cfg->dac_channel_nb), LL_DAC_DMA_REG_DATA_12BITS_RIGHT_ALIGNED);
  data->blk.block_size = sizeof(uint16_t) * 2U * cfg->block_samples;
  data->blk.source_addr_adj = DMA_ADDR_ADJ_INCREMENT; /* walk the memory buffer */
  data->blk.dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;   /* fixed DAC data register */
  data->blk.source_reload_en = 1;                     /* arms cyclic mode in dma_stm32u5 */

  data->dma_cfg = (struct dma_config){0};
  data->dma_cfg.dma_slot = cfg->dma_slot;
  data->dma_cfg.channel_direction = MEMORY_TO_PERIPHERAL;
  data->dma_cfg.source_data_size = 2;
  data->dma_cfg.dest_data_size = 2;
  data->dma_cfg.source_burst_length = 2;
  data->dma_cfg.dest_burst_length = 2;
  data->dma_cfg.block_count = 1;
  data->dma_cfg.head_block = &data->blk;
  data->dma_cfg.dma_callback = aao_dma_cb;
  data->dma_cfg.user_data = (void *)dev;

  int r = dma_config(cfg->dma_dev, cfg->dma_channel, &data->dma_cfg);
  if (r < 0) {
    LOG_ERR("dma_config: %d", r);
    return r;
  }
  /* STM32U5 requires a WORD (32-bit) DMA write to the DAC data register: a
   * half-word write to DHR12R1 raises a GPDMA data-transfer error (DTEF), the
   * channel disables and the DAC underruns. The source stays half-word (12-bit
   * samples in a u16 buffer); only the destination is widened to a word (PAM=0
   * right-aligns the sample and zero-pads it into the 32-bit register write).
   * Zephyr's dma_config() rejects mixed source/dest sizes, so the dest width is
   * overridden here, with the channel disabled between config and start. */
  LL_DMA_SetDestDataWidth(GPDMA1, cfg->dma_channel, LL_DMA_DEST_DATAWIDTH_WORD);
  return dma_start(cfg->dma_dev, cfg->dma_channel);
}

static int aao_timer_start(const struct device *dev) {
  const struct aao_config *cfg = dev->config;
  const struct device *clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
  uint32_t tim_clk = 0;

  (void)clock_control_on(clk, (clock_control_subsys_t)&cfg->tim_pclken[0]);
  int r = clock_control_get_rate(clk, (clock_control_subsys_t)&cfg->tim_pclken[1], &tim_clk);
  if (r < 0 || tim_clk == 0) {
    LOG_ERR("timer clock rate unavailable (r=%d, clk=%u)", r, tim_clk);
    return r < 0 ? r : -EINVAL;
  }

  /* TIM7 is a 16-bit timer: ARR = tim_clk/fs - 1 must fit in [0, 0xFFFF]. Reject
   * a sample rate the timer cannot produce; divider 0 (fs > tim_clk) would also
   * underflow the subtraction. */
  uint32_t div = tim_clk / cfg->sampling_frequency;
  if (div == 0U || div > 0x10000U) {
    LOG_ERR("sample rate %u Hz unattainable from tim_clk %u (divider %u)", cfg->sampling_frequency, tim_clk, div);
    return -EINVAL;
  }
  LL_TIM_SetPrescaler(cfg->tim, 0);
  LL_TIM_SetAutoReload(cfg->tim, div - 1U);
  LL_TIM_SetTriggerOutput(cfg->tim, LL_TIM_TRGO_UPDATE);
  LL_TIM_GenerateEvent_UPDATE(cfg->tim);
  LL_TIM_EnableCounter(cfg->tim);
  return 0;
}

/* Best-effort DAC power-down for start() error paths and stop(): stop the
 * trigger + DMA request and disable the channel so nothing is left half-armed. */
static void aao_dac_disable(DAC_TypeDef *dac, uint32_t ch) {
  LL_DAC_DisableTrigger(dac, ch);
  LL_DAC_DisableDMAReq(dac, ch);
  LL_DAC_Disable(dac, ch);
}

int analog_audio_out_start(const struct device *dev, analog_audio_out_src src, void *user_data) {
  const struct aao_config *cfg = dev->config;
  struct aao_data *data = dev->data;
  int r;

  /* Guard against a never-initialised device (aao_init failed => not ready):
   * refill_work would be uninitialised. */
  if (!device_is_ready(dev)) {
    return -ENODEV;
  }
  if (src == NULL) {
    return -EINVAL;
  }
  if (atomic_get(&data->running)) {
    return -EALREADY;
  }
  data->src = src;
  data->user_data = user_data;
  atomic_set(&data->running, 1);

  /* Arm the memory->DAC DMA FIRST so it is ready to service the DAC's first
   * request; only then enable the DAC (DMA request + trigger). Enabling the DAC
   * before the DMA is armed loses the first request and underruns immediately. */
  r = aao_dma_start(dev);
  if (r < 0) {
    atomic_set(&data->running, 0);
    return r;
  }
  r = aao_dac_setup(dev);
  if (r < 0) {
    dma_stop(cfg->dma_dev, cfg->dma_channel);
    aao_dac_disable(cfg->dac, aao_ll_channel(cfg->dac_channel_nb));
    atomic_set(&data->running, 0);
    return r;
  }
  r = aao_timer_start(dev);
  if (r < 0) {
    dma_stop(cfg->dma_dev, cfg->dma_channel);
    aao_dac_disable(cfg->dac, aao_ll_channel(cfg->dac_channel_nb));
    atomic_set(&data->running, 0);
    return r;
  }
  LOG_INF("playback started");
  return 0;
}

int analog_audio_out_stop(const struct device *dev) {
  const struct aao_config *cfg = dev->config;
  struct aao_data *data = dev->data;

  /* Guard against a never-initialised device: callers (SA818 stream stop) invoke
   * this unconditionally. */
  if (!device_is_ready(dev)) {
    return -ENODEV;
  }

  /* Tear down the hardware only if playback was actually running: stop() can be
   * called after a skipped/failed start, where touching the TIM/DAC/DMA
   * registers would be wrong. Also power the DAC down (not just stop the
   * trigger), mirroring the error-path teardown. */
  if (atomic_get(&data->running)) {
    atomic_set(&data->running, 0);
    LL_TIM_DisableCounter(cfg->tim);
    aao_dac_disable(cfg->dac, aao_ll_channel(cfg->dac_channel_nb));
    dma_stop(cfg->dma_dev, cfg->dma_channel);
  }
  return 0;
}

static int aao_init(const struct device *dev) {
  const struct aao_config *cfg = dev->config;
  struct aao_data *data = dev->data;

  LOG_INF("init: %u Hz, %u-bit, block=%u", cfg->sampling_frequency, cfg->resolution, cfg->block_samples);
  if (cfg->block_samples == 0 || cfg->block_samples > AAO_MAX_BLOCK) {
    LOG_ERR("block-samples %u out of range (1..%u)", cfg->block_samples, AAO_MAX_BLOCK);
    return -EINVAL;
  }
  if (cfg->sampling_frequency == 0) {
    LOG_ERR("sampling-frequency must be non-zero");
    return -EINVAL;
  }
  if (cfg->resolution == 0 || cfg->resolution > 16) {
    LOG_ERR("resolution %u out of range (1..16)", cfg->resolution);
    return -EINVAL;
  }
  if (!device_is_ready(cfg->dma_dev)) {
    LOG_ERR("dma device not ready");
    return -ENODEV;
  }
  data->self = dev;
  k_work_init(&data->refill_work, aao_refill_work);
  return 0;
}

#define AAO_INIT(inst)                                                                                                                                         \
  /* The DAC trigger is hardcoded to TIM7-TRGO (aao_dac_setup), so the DT-selected                                                                             \
   * sampling-timer must be TIM7. Enforce at build time via node identity                                                                                      \
   * (security-agnostic; a runtime base-address compare is unreliable because TIM7                                                                             \
   * aliases to different secure/non-secure addresses on STM32U5). */                                                                                          \
  BUILD_ASSERT(DT_SAME_NODE(DT_INST_PHANDLE(inst, sampling_timer), DT_NODELABEL(timers7)),                                                                     \
               "analog-audio-out sampling-timer must be TIM7 (DAC trigger is hardcoded to TIM7-TRGO)");                                                        \
  static const struct stm32_pclken aao_tim_pclken_##inst[] = STM32_DT_CLOCKS(DT_INST_PHANDLE(inst, sampling_timer));                                           \
  static const struct stm32_pclken aao_dac_pclken_##inst[] = STM32_DT_CLOCKS(DT_INST_IO_CHANNELS_CTLR(inst));                                                  \
  static const struct aao_config aao_cfg_##inst = {                                                                                                            \
      .sampling_frequency = DT_INST_PROP(inst, sampling_frequency),                                                                                            \
      .block_samples = DT_INST_PROP(inst, block_samples),                                                                                                      \
      .resolution = DT_INST_PROP(inst, resolution),                                                                                                            \
      .dac_channel_nb = DT_INST_IO_CHANNELS_OUTPUT(inst),                                                                                                      \
      .tim = (TIM_TypeDef *)DT_REG_ADDR(DT_INST_PHANDLE(inst, sampling_timer)),                                                                                \
      .dac = (DAC_TypeDef *)DT_REG_ADDR(DT_INST_IO_CHANNELS_CTLR(inst)),                                                                                       \
      .tim_pclken = aao_tim_pclken_##inst,                                                                                                                     \
      .dac_pclken = aao_dac_pclken_##inst,                                                                                                                     \
      .dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(inst, tx)),                                                                                           \
      .dma_channel = DT_INST_DMAS_CELL_BY_NAME(inst, tx, channel),                                                                                             \
      .dma_slot = DT_INST_DMAS_CELL_BY_NAME(inst, tx, slot),                                                                                                   \
  };                                                                                                                                                           \
  static struct aao_data aao_data_##inst;                                                                                                                      \
  DEVICE_DT_INST_DEFINE(inst, aao_init, NULL, &aao_data_##inst, &aao_cfg_##inst, POST_KERNEL, CONFIG_ANALOG_AUDIO_OUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(AAO_INIT)
