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

#define AAI_INIT(inst)                                                                                                                                         \
  static const struct aai_config aai_cfg_##inst = {                                                                                                            \
      .sampling_frequency = DT_INST_PROP(inst, sampling_frequency),                                                                                            \
      .block_samples = DT_INST_PROP(inst, block_samples),                                                                                                      \
      .resolution = DT_INST_PROP(inst, resolution),                                                                                                            \
  };                                                                                                                                                           \
  static struct aai_data aai_data_##inst;                                                                                                                      \
  DEVICE_DT_INST_DEFINE(inst, aai_init, NULL, &aai_data_##inst, &aai_cfg_##inst, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(AAI_INIT)
