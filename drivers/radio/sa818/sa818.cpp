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

/* =========================
 * Constants
 * ========================= */
namespace {
constexpr uint32_t kInitDelayMs = 10U;
}

/* =========================
 * Driver data structures
 * ========================= */

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

struct sa818_data {
  bool powered;
  bool ptt_active;
};

/* =========================
 * Internal helpers
 * ========================= */

static int sa818_gpio_init(const sa818_config *cfg) {
  if (!device_is_ready(cfg->h_l_power.port) || !device_is_ready(cfg->nptt.port) ||
      !device_is_ready(cfg->npower_down.port) || !device_is_ready(cfg->nsquelch.port)) {
    return -ENODEV;
  }

  gpio_pin_configure_dt(&cfg->h_l_power, GPIO_OUTPUT_INACTIVE);
  gpio_pin_configure_dt(&cfg->nptt, GPIO_OUTPUT_INACTIVE);
  gpio_pin_configure_dt(&cfg->npower_down, GPIO_OUTPUT_ACTIVE);
  gpio_pin_configure_dt(&cfg->nsquelch, GPIO_INPUT);

  return 0;
}

/* =========================
 * Init
 * ========================= */

static int sa818_init(const struct device *dev) {
  const sa818_config *cfg = static_cast<const sa818_config *>(dev->config);
  sa818_data *data = static_cast<sa818_data *>(dev->data);

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

  data->powered = false;
  data->ptt_active = false;

  k_msleep(kInitDelayMs);

  LOG_INF("SA818 initialized");
  return 0;
}

/* =========================
 * Public API
 * ========================= */

int sa818_power_on(const struct device *dev) {
  auto *data = static_cast<sa818_data *>(dev->data);
  auto *cfg = static_cast<const sa818_config *>(dev->config);

  gpio_pin_set_dt(&cfg->npower_down, 0);
  k_msleep(cfg->rx_settle_time_ms);

  data->powered = true;
  LOG_INF("SA818 powered on");
  return 0;
}

int sa818_power_off(const struct device *dev) {
  auto *data = static_cast<sa818_data *>(dev->data);
  auto *cfg = static_cast<const sa818_config *>(dev->config);

  gpio_pin_set_dt(&cfg->npower_down, 1);

  data->powered = false;
  LOG_INF("SA818 powered off");
  return 0;
}

int sa818_set_ptt(const struct device *dev, bool enable) {
  auto *data = static_cast<sa818_data *>(dev->data);
  auto *cfg = static_cast<const sa818_config *>(dev->config);

  gpio_pin_set_dt(&cfg->nptt, enable ? 0 : 1);

  if (enable) {
    k_msleep(cfg->tx_enable_delay_ms);
  }

  data->ptt_active = enable;
  LOG_INF("PTT %s", enable ? "ON" : "OFF");
  return 0;
}

int sa818_set_high_power(const struct device *dev, bool enable) {
  auto *cfg = static_cast<const sa818_config *>(dev->config);

  gpio_pin_set_dt(&cfg->h_l_power, enable ? 1 : 0);
  LOG_INF("TX power %s", enable ? "HIGH" : "LOW");
  return 0;
}

/* =========================
 * Device Tree glue
 * ========================= */

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define SA818_DEFINE(node_id)                                                                      \
  static sa818_data sa818_data_##node_id;                                                          \
                                                                                                   \
  static const sa818_config sa818_config_##node_id = {                                             \
      .uart = DEVICE_DT_GET(DT_PHANDLE(node_id, uart)),                                            \
      .audio_in = ADC_DT_SPEC_GET_BY_IDX(node_id, 0),                                              \
      .h_l_power = GPIO_DT_SPEC_GET(node_id, h_l_power_gpios),                                     \
      .nptt = GPIO_DT_SPEC_GET(node_id, nptt_gpios),                                               \
      .npower_down = GPIO_DT_SPEC_GET(node_id, npower_down_gpios),                                 \
      .nsquelch = GPIO_DT_SPEC_GET(node_id, nsquelch_gpios),                                       \
      .tx_enable_delay_ms = DT_PROP(node_id, tx_enable_delay_ms),                                  \
      .rx_settle_time_ms = DT_PROP(node_id, rx_settle_time_ms),                                    \
  };                                                                                               \
                                                                                                   \
  DEVICE_DT_DEFINE(node_id, sa818_init, nullptr, &sa818_data_##node_id, &sa818_config_##node_id,   \
                   POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, nullptr);

DT_FOREACH_STATUS_OKAY(DT_DRV_COMPAT, SA818_DEFINE)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
