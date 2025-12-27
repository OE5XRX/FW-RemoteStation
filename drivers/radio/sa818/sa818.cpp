#define DT_DRV_COMPAT sa_sa818

#include <sa818/sa818.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sa818, LOG_LEVEL_INF);

namespace {
constexpr uint32_t kInitDelayMs = 10U;
}

struct sa818_config {
  const struct device *uart;
  struct adc_dt_spec audio_in;

  struct gpio_dt_spec h_l_power;
  struct gpio_dt_spec nptt;
  struct gpio_dt_spec npower_down;
  struct gpio_dt_spec nsquelch;

  uint32_t tx_enable_delay_ms;
  uint32_t rx_settle_time_ms;
};

static int sa818_gpio_init(const sa818_config *cfg) {
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

static int sa818_init(const struct device *dev) {
  const sa818_config *cfg = static_cast<const sa818_config *>(dev->config);

  if (!device_is_ready(cfg->uart)) {
    LOG_ERR("UART not ready");
    return -ENODEV;
  }

  if (!adc_is_ready_dt(&cfg->audio_in)) {
    LOG_ERR("ADC not ready");
    return -ENODEV;
  }

  int ret = sa818_gpio_init(cfg);
  if (ret != 0) {
    LOG_ERR("GPIO init failed");
    return ret;
  }

  sa818_status *data = static_cast<sa818_status *>(dev->data);
  data->device_power = SA818_DEVICE_OFF;
  data->ptt_state = SA818_PTT_OFF;
  data->power_level = SA818_POWER_LOW;
  data->squelch = false;

  k_msleep(kInitDelayMs);

  LOG_INF("SA818 initialized");
  return 0;
}

int sa818_set_power(const struct device *dev, sa818_device_power power_state) {
  auto *cfg = static_cast<const sa818_config *>(dev->config);

  if (power_state == SA818_DEVICE_ON) {
    gpio_pin_set_dt(&cfg->npower_down, 0);
    LOG_INF("SA818 powered on");
  } else {
    gpio_pin_set_dt(&cfg->npower_down, 1);
    LOG_INF("SA818 powered off");
  }

  auto *data = static_cast<sa818_status *>(dev->data);
  data->device_power = power_state;
  LOG_INF("SA818 powered %s", power_state == SA818_DEVICE_ON ? "ON" : "OFF");
  return 0;
}

int sa818_set_ptt(const struct device *dev, sa818_ptt_state ptt_state) {
  auto *cfg = static_cast<const sa818_config *>(dev->config);

  if (ptt_state == SA818_PTT_ON) {
    gpio_pin_set_dt(&cfg->nptt, 1);
  } else {
    gpio_pin_set_dt(&cfg->nptt, 0);
  }

  if (ptt_state == SA818_PTT_ON) {
    k_msleep(cfg->tx_enable_delay_ms);
  }

  auto *data = static_cast<sa818_status *>(dev->data);
  data->ptt_state = ptt_state;
  LOG_INF("PTT %s", ptt_state == SA818_PTT_ON ? "ON" : "OFF");
  return 0;
}

int sa818_set_power_level(const struct device *dev, sa818_power_level power_level) {
  auto *cfg = static_cast<const sa818_config *>(dev->config);

  if (power_level == SA818_POWER_HIGH) {
    gpio_pin_set_dt(&cfg->h_l_power, 1);
  } else {
    gpio_pin_set_dt(&cfg->h_l_power, 0);
  }

  auto *data = static_cast<sa818_status *>(dev->data);
  data->power_level = power_level;
  LOG_INF("TX power %s", power_level == SA818_POWER_HIGH ? "HIGH" : "LOW");
  return 0;
}

bool sa818_is_squelch_open(const struct device *dev) {
  auto *cfg = static_cast<const sa818_config *>(dev->config);
  int val = gpio_pin_get_dt(&cfg->nsquelch);
  return (val > 0);
}

sa818_status sa818_get_status(const struct device *dev) {
  auto *data = static_cast<sa818_status *>(dev->data);
  sa818_status status;
  status.device_power = data->device_power;
  status.ptt_state = data->ptt_state;
  status.power_level = data->power_level;
  status.squelch = sa818_is_squelch_open(dev);
  return status;
}

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define SA818_DEFINE(node_id)                                                                                                                                  \
  static sa818_status sa818_status_##node_id;                                                                                                                  \
                                                                                                                                                               \
  static const sa818_config sa818_config_##node_id = {                                                                                                         \
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
  DEVICE_DT_DEFINE(node_id, sa818_init, nullptr, &sa818_status_##node_id, &sa818_config_##node_id, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, nullptr);

DT_FOREACH_STATUS_OKAY(DT_DRV_COMPAT, SA818_DEFINE)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
