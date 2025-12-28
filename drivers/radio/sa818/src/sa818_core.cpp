/**
 * @file sa818_core.cpp
 * @brief SA818 Core Driver Implementation
 *
 * Implements device initialization, GPIO control, power management,
 * PTT control, and status monitoring for SA818 radio module.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#define DT_DRV_COMPAT sa_sa818

#include "sa818_priv.h"

#include <sa818/sa818.h>
#include <sa818/sa818_at.h>
#include <sa818/sa818_audio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sa818_core, LOG_LEVEL_INF);

/* GPIO Initialization */
int sa818_gpio_init(const struct sa818_config *cfg) {
  if (!device_is_ready(cfg->h_l_power.port) || !device_is_ready(cfg->nptt.port) || !device_is_ready(cfg->npower_down.port) ||
      !device_is_ready(cfg->nsquelch.port)) {
    return -ENODEV;
  }

  gpio_pin_configure_dt(&cfg->h_l_power, GPIO_OUTPUT_INACTIVE);
  gpio_pin_configure_dt(&cfg->nptt, GPIO_OUTPUT_INACTIVE);
  gpio_pin_configure_dt(&cfg->npower_down, GPIO_OUTPUT_ACTIVE);
  gpio_pin_configure_dt(&cfg->nsquelch, GPIO_INPUT);

  return 0;
}

/* Device Initialization */
static int sa818_init(const struct device *dev) {
  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(dev->config);
  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);

  /* Verify UART is ready */
  if (!device_is_ready(cfg->uart)) {
    LOG_ERR("UART not ready");
    return -ENODEV;
  }

  /* Verify ADC is ready */
  if (!adc_is_ready_dt(&cfg->audio_in)) {
    LOG_ERR("ADC not ready");
    return -ENODEV;
  }

  /* Initialize GPIO pins */
  int ret = sa818_gpio_init(cfg);
  if (ret != 0) {
    LOG_ERR("GPIO init failed: %d", ret);
    return ret;
  }

  /* Initialize synchronization primitives */
  k_mutex_init(&data->lock);
  k_sem_init(&data->at_response_sem, 0, 1);

  /* Initialize device state */
  data->device_power = SA818_DEVICE_OFF;
  data->ptt_state = SA818_PTT_OFF;
  data->power_level = SA818_POWER_LOW;
  data->squelch = false;
  data->audio_rx_enabled = false;
  data->audio_tx_enabled = false;
  data->current_volume = 4; // Default mid-level
  data->at_response_len = 0;

  /* Give hardware time to stabilize */
  k_msleep(SA818_INIT_DELAY_MS);

  /* Initialize audio subsystem */
  ret = sa818_audio_init(dev);
  if (ret != 0) {
    LOG_WRN("Audio init failed: %d", ret);
    // Non-fatal, continue
  }

  LOG_INF("SA818 initialized");
  return 0;
}

/* Power Control */
sa818_result sa818_set_power(const struct device *dev, sa818_device_power power_state) {
  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(dev->config);
  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);

  k_mutex_lock(&data->lock, K_FOREVER);

  if (power_state == SA818_DEVICE_ON) {
    gpio_pin_set_dt(&cfg->npower_down, 0); // Active LOW
    k_msleep(SA818_POWER_ON_DELAY_MS);     // Wait for module to power up
    LOG_INF("SA818 powered ON");
  } else {
    gpio_pin_set_dt(&cfg->npower_down, 1);
    LOG_INF("SA818 powered OFF");
  }

  data->device_power = power_state;
  k_mutex_unlock(&data->lock);

  return SA818_OK;
}

/* PTT Control */
sa818_result sa818_set_ptt(const struct device *dev, sa818_ptt_state ptt_state) {
  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(dev->config);
  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);

  k_mutex_lock(&data->lock, K_FOREVER);

  if (ptt_state == SA818_PTT_ON) {
    gpio_pin_set_dt(&cfg->nptt, 1); // Active HIGH (inverted by pin name)
    k_msleep(cfg->tx_enable_delay_ms);
    LOG_INF("PTT ON");
  } else {
    gpio_pin_set_dt(&cfg->nptt, 0);
    LOG_INF("PTT OFF");
  }

  data->ptt_state = ptt_state;
  k_mutex_unlock(&data->lock);

  return SA818_OK;
}

/* Power Level Control */
sa818_result sa818_set_power_level(const struct device *dev, sa818_power_level power_level) {
  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(dev->config);
  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);

  k_mutex_lock(&data->lock, K_FOREVER);

  if (power_level == SA818_POWER_HIGH) {
    gpio_pin_set_dt(&cfg->h_l_power, 1);
    LOG_INF("TX power HIGH");
  } else {
    gpio_pin_set_dt(&cfg->h_l_power, 0);
    LOG_INF("TX power LOW");
  }

  data->power_level = power_level;
  k_mutex_unlock(&data->lock);

  return SA818_OK;
}

/* Squelch Status */
sa818_squelch_state sa818_get_squelch(const struct device *dev) {
  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(dev->config);
  int val = gpio_pin_get_dt(&cfg->nsquelch);
  // SQL pin is active HIGH when squelch is open (no signal)
  return (val > 0) ? SA818_SQUELCH_OPEN : SA818_SQUELCH_CLOSED;
}

/* Status Query */
sa818_status sa818_get_status(const struct device *dev) {
  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);
  sa818_status status;

  k_mutex_lock(&data->lock, K_FOREVER);
  status.device_power = data->device_power;
  status.ptt_state = data->ptt_state;
  status.power_level = data->power_level;
  status.squelch_state = sa818_get_squelch(dev);
  status.volume = data->current_volume;
  k_mutex_unlock(&data->lock);

  return status;
}

/* Device Definition Macro */
#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define SA818_DEFINE(node_id)                                                                                                                                  \
  static struct sa818_data sa818_data_##node_id;                                                                                                               \
                                                                                                                                                               \
  static const struct sa818_config sa818_config_##node_id = {                                                                                                  \
      .uart = DEVICE_DT_GET(DT_PHANDLE(node_id, uart)),                                                                                                        \
      .audio_in = ADC_DT_SPEC_GET_BY_IDX(node_id, 0),                                                                                                          \
      .h_l_power = GPIO_DT_SPEC_GET(node_id, h_l_power_gpios),                                                                                                 \
      .nptt = GPIO_DT_SPEC_GET(node_id, nptt_gpios),                                                                                                           \
      .npower_down = GPIO_DT_SPEC_GET(node_id, npower_down_gpios),                                                                                             \
      .nsquelch = GPIO_DT_SPEC_GET(node_id, nsquelch_gpios),                                                                                                   \
      .tx_enable_delay_ms = DT_PROP(node_id, tx_enable_delay_ms),                                                                                              \
      .rx_settle_time_ms = DT_PROP(node_id, rx_settle_time_ms),                                                                                                \
  };                                                                                                                                                           \
                                                                                                                                                               \
  DEVICE_DT_DEFINE(node_id, sa818_init, nullptr, &sa818_data_##node_id, &sa818_config_##node_id, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, nullptr);

DT_FOREACH_STATUS_OKAY(DT_DRV_COMPAT, SA818_DEFINE)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
