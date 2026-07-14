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
      .sampling_frequency = DT_INST_PROP(inst, sampling_frequency),                                                                                            \
      .block_samples = DT_INST_PROP(inst, block_samples),                                                                                                      \
      .resolution = DT_INST_PROP(inst, resolution),                                                                                                            \
  };                                                                                                                                                           \
  static struct aao_data aao_data_##inst;                                                                                                                      \
  DEVICE_DT_INST_DEFINE(inst, aao_init, NULL, &aao_data_##inst, &aao_cfg_##inst, POST_KERNEL, CONFIG_ANALOG_AUDIO_OUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(AAO_INIT)
