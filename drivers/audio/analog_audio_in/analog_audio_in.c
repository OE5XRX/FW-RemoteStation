/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#define DT_DRV_COMPAT oe5xrx_analog_audio_in

#include "adc_pcm.h"

#include <oe5xrx/audio/analog_audio_in.h>
#include <stm32_ll_adc.h>
#include <stm32_ll_tim.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(analog_audio_in, CONFIG_ANALOG_AUDIO_IN_LOG_LEVEL);

/* Max samples per DMA half-buffer; the circular buffer is 2x this. */
#define AAI_MAX_BLOCK 16
/* Depth of the ISR->thread hand-off queue, in blocks (~1 ms each at 8 kHz). */
#define AAI_MSGQ_BLOCKS 16

struct aai_config {
  uint32_t sampling_frequency;
  uint16_t block_samples;
  uint8_t resolution;
  uint32_t adc_ll_channel; /* LL_ADC_CHANNEL_x derived from io-channels */
  TIM_TypeDef *tim;
  ADC_TypeDef *adc;
  const struct stm32_pclken *tim_pclken; /* [0]=bus enable, [1]=kernel clock */
  const struct stm32_pclken *adc_pclken; /* [0]=bus enable, [1]=kernel clock */
  const struct device *dma_dev;
  uint32_t dma_channel;
  uint32_t dma_slot;
};

struct aai_data {
  const struct device *self;
  analog_audio_in_cb cb;
  void *user_data;
  bool running;
  uint16_t dma_buf[2 * AAI_MAX_BLOCK]; /* circular: [0..block) | [block..2*block) */
  /* ISR -> thread hand-off: the DMA callback runs in ISR context but the
   * consumer callback may block (e.g. take a mutex), so blocks of converted PCM
   * are queued here and delivered from the system workqueue thread. */
  struct k_msgq rx_msgq;
  char msgq_buf[AAI_MSGQ_BLOCKS * AAI_MAX_BLOCK * sizeof(int16_t)];
  struct k_work drain_work;
  struct dma_config dma_cfg;
  struct dma_block_config blk;
};

/* Bounded wait: returns 0 when cond() true within ~timeout_us, else -ETIMEDOUT. */
#define AAI_WAIT(cond, timeout_us)                                                                                                                             \
  ({                                                                                                                                                           \
    int _to = (timeout_us);                                                                                                                                    \
    while (!(cond) && _to > 0) {                                                                                                                               \
      k_busy_wait(10);                                                                                                                                         \
      _to -= 10;                                                                                                                                               \
    }                                                                                                                                                          \
    (cond) ? 0 : -ETIMEDOUT;                                                                                                                                   \
  })

/* System-workqueue handler: drain queued PCM blocks and deliver them to the
 * consumer in thread context (safe to block/take a mutex). */
static void aai_drain_work(struct k_work *work) {
  struct aai_data *data = CONTAINER_OF(work, struct aai_data, drain_work);
  const struct aai_config *cfg = data->self->config;
  int16_t block[AAI_MAX_BLOCK];

  while (k_msgq_get(&data->rx_msgq, block, K_NO_WAIT) == 0) {
    if (data->cb != NULL) {
      data->cb(block, cfg->block_samples, data->user_data);
    }
  }
}

static void aai_dma_cb(const struct device *dma_dev, void *user, uint32_t channel, int status) {
  const struct device *dev = user;
  const struct aai_config *cfg = dev->config;
  struct aai_data *data = dev->data;
  int16_t block[AAI_MAX_BLOCK];

  ARG_UNUSED(dma_dev);
  ARG_UNUSED(channel);

  if (!data->running) {
    return;
  }
  /* Only the half/full-transfer completions carry a ready buffer half.
   * DMA_STATUS_BLOCK = first half ready, DMA_STATUS_COMPLETE = second half.
   * Ignore errors (status < 0) and any other/unexpected status so we never
   * read from the wrong half and enqueue corrupted samples. */
  const uint16_t *src;
  if (status == DMA_STATUS_BLOCK) {
    src = &data->dma_buf[0];
  } else if (status == DMA_STATUS_COMPLETE) {
    src = &data->dma_buf[cfg->block_samples];
  } else {
    return;
  }
  for (uint16_t i = 0; i < cfg->block_samples; i++) {
    block[i] = adc_to_pcm16(src[i], cfg->resolution);
  }
  /* Hand off to the workqueue thread; drop the block if the queue is full
   * (consumer not keeping up) rather than block the ISR. */
  if (k_msgq_put(&data->rx_msgq, block, K_NO_WAIT) == 0) {
    k_work_submit(&data->drain_work);
  }
}

static uint32_t aai_ll_resolution(uint8_t bits) {
  switch (bits) {
  case 14:
    return LL_ADC_RESOLUTION_14B;
  case 12:
    return LL_ADC_RESOLUTION_12B;
  case 10:
    return LL_ADC_RESOLUTION_10B;
  case 8:
    return LL_ADC_RESOLUTION_8B;
  case 6:
    return LL_ADC_RESOLUTION_6B;
  default:
    return LL_ADC_RESOLUTION_12B;
  }
}

static int aai_adc_setup(const struct device *dev) {
  const struct aai_config *cfg = dev->config;
  const struct device *clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
  ADC_TypeDef *adc = cfg->adc;
  int r;

  /* Enable ADC bus + kernel (async) clock. */
  (void)clock_control_on(clk, (clock_control_subsys_t)&cfg->adc_pclken[0]);
  r = clock_control_configure(clk, (clock_control_subsys_t)&cfg->adc_pclken[1], NULL);
  if (r < 0) {
    LOG_ERR("adc kernel clock cfg: %d", r);
    return r;
  }
  LL_ADC_SetCommonClock(__LL_ADC_COMMON_INSTANCE(adc), LL_ADC_CLOCK_ASYNC_DIV2);

  /* Wake + regulator + calibrate. */
  LL_ADC_DisableDeepPowerDown(adc);
  LL_ADC_EnableInternalRegulator(adc);
  k_busy_wait(LL_ADC_DELAY_INTERNAL_REGUL_STAB_US);

  LL_ADC_StartCalibration(adc, LL_ADC_CALIB_OFFSET);
  r = AAI_WAIT(LL_ADC_IsCalibrationOnGoing(adc) == 0, 10000);
  if (r < 0) {
    LOG_ERR("adc calibration timeout");
    return r;
  }

  /* One-channel regular sequence on the DT-selected channel, TIM6-TRGO triggered. */
  LL_ADC_SetResolution(adc, aai_ll_resolution(cfg->resolution));
  LL_ADC_REG_SetSequencerLength(adc, LL_ADC_REG_SEQ_SCAN_DISABLE);
  /* STM32U5: the channel must be enabled in PCSEL to connect the analog input,
   * otherwise conversions return a fixed value instead of the pin voltage. This
   * is set here (not delegated to the co-bound Zephyr adc driver) so capture
   * does not depend on anything else having configured the channel first. */
  LL_ADC_SetChannelPreselection(adc, cfg->adc_ll_channel);
  LL_ADC_REG_SetSequencerRanks(adc, LL_ADC_REG_RANK_1, cfg->adc_ll_channel);
  LL_ADC_SetChannelSamplingTime(adc, cfg->adc_ll_channel, LL_ADC_SAMPLINGTIME_391CYCLES_5);
  LL_ADC_REG_SetTriggerSource(adc, LL_ADC_REG_TRIG_EXT_TIM6_TRGO);
  LL_ADC_REG_SetTriggerEdge(adc, LL_ADC_REG_TRIG_EXT_RISING);
  /* UNLIMITED: keep issuing a DMA request per TRGO-triggered conversion so the
   * circular DMA runs continuously (LIMITED stops after the first sequence). */
  LL_ADC_REG_SetDataTransferMode(adc, LL_ADC_REG_DMA_TRANSFER_UNLIMITED);
  LL_ADC_REG_SetOverrun(adc, LL_ADC_REG_OVR_DATA_OVERWRITTEN);

  /* Enable + wait ADRDY. */
  LL_ADC_ClearFlag_ADRDY(adc);
  LL_ADC_Enable(adc);
  r = AAI_WAIT(LL_ADC_IsActiveFlag_ADRDY(adc) != 0, 10000);
  if (r < 0) {
    LOG_ERR("adc ADRDY timeout");
    return r;
  }
  return 0;
}

static int aai_dma_start(const struct device *dev) {
  const struct aai_config *cfg = dev->config;
  struct aai_data *data = dev->data;

  data->blk = (struct dma_block_config){0};
  data->blk.source_address = LL_ADC_DMA_GetRegAddr(cfg->adc, LL_ADC_DMA_REG_REGULAR_DATA);
  data->blk.dest_address = (uint32_t)(uintptr_t)data->dma_buf;
  data->blk.block_size = sizeof(uint16_t) * 2U * cfg->block_samples;
  data->blk.source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
  data->blk.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;
  data->blk.source_reload_en = 1; /* GPDMA "emulated circular" */

  data->dma_cfg = (struct dma_config){0};
  data->dma_cfg.dma_slot = cfg->dma_slot;
  data->dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
  data->dma_cfg.source_data_size = 2;
  data->dma_cfg.dest_data_size = 2;
  /* burst length must be a multiple of the data size; 1 beat = data_size bytes. */
  data->dma_cfg.source_burst_length = 2;
  data->dma_cfg.dest_burst_length = 2;
  data->dma_cfg.block_count = 1;
  data->dma_cfg.head_block = &data->blk;
  data->dma_cfg.dma_callback = aai_dma_cb;
  data->dma_cfg.user_data = (void *)dev;

  int r = dma_config(cfg->dma_dev, cfg->dma_channel, &data->dma_cfg);
  if (r < 0) {
    LOG_ERR("dma_config: %d", r);
    return r;
  }
  return dma_start(cfg->dma_dev, cfg->dma_channel);
}

static int aai_timer_start(const struct device *dev) {
  const struct aai_config *cfg = dev->config;
  const struct device *clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
  uint32_t tim_clk = 0;

  (void)clock_control_on(clk, (clock_control_subsys_t)&cfg->tim_pclken[0]);
  int r = clock_control_get_rate(clk, (clock_control_subsys_t)&cfg->tim_pclken[1], &tim_clk);
  if (r < 0 || tim_clk == 0) {
    LOG_ERR("timer clock rate unavailable (r=%d, clk=%u)", r, tim_clk);
    return r < 0 ? r : -EINVAL;
  }

  LL_TIM_SetPrescaler(cfg->tim, 0);
  LL_TIM_SetAutoReload(cfg->tim, (tim_clk / cfg->sampling_frequency) - 1U);
  LL_TIM_SetTriggerOutput(cfg->tim, LL_TIM_TRGO_UPDATE);
  LL_TIM_GenerateEvent_UPDATE(cfg->tim);
  LL_TIM_EnableCounter(cfg->tim);
  return 0;
}

int analog_audio_in_start(const struct device *dev, analog_audio_in_cb cb, void *user_data) {
  struct aai_data *data = dev->data;
  int r;

  if (cb == NULL) {
    return -EINVAL;
  }
  if (data->running) {
    return -EALREADY;
  }
  data->cb = cb;
  data->user_data = user_data;
  k_msgq_purge(&data->rx_msgq);
  data->running = true;

  r = aai_adc_setup(dev);
  if (r < 0) {
    data->running = false;
    return r;
  }
  r = aai_dma_start(dev);
  if (r < 0) {
    data->running = false;
    return r;
  }
  r = aai_timer_start(dev);
  if (r < 0) {
    dma_stop(((const struct aai_config *)dev->config)->dma_dev, ((const struct aai_config *)dev->config)->dma_channel);
    data->running = false;
    return r;
  }
  LL_ADC_REG_StartConversion(((const struct aai_config *)dev->config)->adc);
  LOG_INF("capture started");
  return 0;
}

int analog_audio_in_stop(const struct device *dev) {
  const struct aai_config *cfg = dev->config;
  struct aai_data *data = dev->data;

  /* Tear down the hardware only if capture was actually running: stop() can be
   * called after a skipped/failed start (e.g. device not ready), where touching
   * the TIM/ADC/DMA registers would be wrong. */
  if (data->running) {
    data->running = false;
    LL_TIM_DisableCounter(cfg->tim);
    LL_ADC_REG_StopConversion(cfg->adc);
    dma_stop(cfg->dma_dev, cfg->dma_channel);
  }
  /* Always drop queued blocks and clear the callback (even if never started) so
   * a drain_work item that was already submitted cannot deliver stale samples. */
  k_msgq_purge(&data->rx_msgq);
  data->cb = NULL;
  data->user_data = NULL;
  return 0;
}

static int aai_init(const struct device *dev) {
  const struct aai_config *cfg = dev->config;
  struct aai_data *data = dev->data;

  LOG_INF("init: %u Hz, %u-bit, block=%u", cfg->sampling_frequency, cfg->resolution, cfg->block_samples);
  if (cfg->block_samples == 0 || cfg->block_samples > AAI_MAX_BLOCK) {
    LOG_ERR("block-samples %u out of range (1..%u)", cfg->block_samples, AAI_MAX_BLOCK);
    return -EINVAL;
  }
  if (cfg->sampling_frequency == 0) {
    LOG_ERR("sampling-frequency must be non-zero");
    return -EINVAL;
  }
  switch (cfg->resolution) {
  case 6:
  case 8:
  case 10:
  case 12:
  case 14:
    break;
  default:
    LOG_ERR("unsupported resolution %u (need 6/8/10/12/14)", cfg->resolution);
    return -EINVAL;
  }
  if (!device_is_ready(cfg->dma_dev)) {
    LOG_ERR("dma device not ready");
    return -ENODEV;
  }
  data->self = dev;
  /* Size the queue element to the configured block so put()/get() copy exactly
   * block_samples and never carry uninitialized tail bytes of the ISR buffer. */
  k_msgq_init(&data->rx_msgq, data->msgq_buf, cfg->block_samples * sizeof(int16_t), AAI_MSGQ_BLOCKS);
  k_work_init(&data->drain_work, aai_drain_work);
  return 0;
}

#define AAI_INIT(inst)                                                                                                                                         \
  /* The ADC regular trigger is hardcoded to TIM6-TRGO (aai_adc_setup), so the                                                                                 \
   * DT-selected sampling-timer must be TIM6. Enforce at build time via node                                                                                   \
   * identity (security-agnostic; a runtime base-address compare is unreliable                                                                                 \
   * because TIM6 aliases to different secure/non-secure addresses on STM32U5). */                                                                             \
  BUILD_ASSERT(DT_SAME_NODE(DT_INST_PHANDLE(inst, sampling_timer), DT_NODELABEL(timers6)),                                                                     \
               "analog-audio-in sampling-timer must be TIM6 (ADC trigger is hardcoded to TIM6-TRGO)");                                                         \
  static const struct stm32_pclken aai_tim_pclken_##inst[] = STM32_DT_CLOCKS(DT_INST_PHANDLE(inst, sampling_timer));                                           \
  static const struct stm32_pclken aai_adc_pclken_##inst[] = STM32_DT_CLOCKS(DT_INST_IO_CHANNELS_CTLR(inst));                                                  \
  static const struct aai_config aai_cfg_##inst = {                                                                                                            \
      .sampling_frequency = DT_INST_PROP(inst, sampling_frequency),                                                                                            \
      .block_samples = DT_INST_PROP(inst, block_samples),                                                                                                      \
      .resolution = DT_INST_PROP(inst, resolution),                                                                                                            \
      .adc_ll_channel = __LL_ADC_DECIMAL_NB_TO_CHANNEL(DT_INST_IO_CHANNELS_INPUT(inst)),                                                                       \
      .tim = (TIM_TypeDef *)DT_REG_ADDR(DT_INST_PHANDLE(inst, sampling_timer)),                                                                                \
      .adc = (ADC_TypeDef *)DT_REG_ADDR(DT_INST_IO_CHANNELS_CTLR(inst)),                                                                                       \
      .tim_pclken = aai_tim_pclken_##inst,                                                                                                                     \
      .adc_pclken = aai_adc_pclken_##inst,                                                                                                                     \
      .dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(inst, rx)),                                                                                           \
      .dma_channel = DT_INST_DMAS_CELL_BY_NAME(inst, rx, channel),                                                                                             \
      .dma_slot = DT_INST_DMAS_CELL_BY_NAME(inst, rx, slot),                                                                                                   \
  };                                                                                                                                                           \
  static struct aai_data aai_data_##inst;                                                                                                                      \
  DEVICE_DT_INST_DEFINE(inst, aai_init, NULL, &aai_data_##inst, &aai_cfg_##inst, POST_KERNEL, CONFIG_ANALOG_AUDIO_IN_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(AAI_INIT)
