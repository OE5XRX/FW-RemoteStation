/**
 * @file sa818_priv.h
 * @brief SA818 Private Definitions and Structures
 *
 * Internal header containing shared structures, constants,
 * and definitions used across SA818 driver modules.
 * Not part of the public API.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#ifndef ZEPHYR_DRIVERS_SA818_SRC_SA818_PRIV_H_
#define ZEPHYR_DRIVERS_SA818_SRC_SA818_PRIV_H_

#include <sa818/sa818.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Private structures and definitions shared between SA818 modules
 */

/* AT Command timeouts and constants */
#define SA818_AT_TIMEOUT_MS 2000
#define SA818_AT_RESPONSE_MAX_LEN 128
#define SA818_UART_BAUDRATE 9600

/* Initialization delays */
#define SA818_INIT_DELAY_MS 10
#define SA818_POWER_ON_DELAY_MS 100

/**
 * @brief SA818 device configuration (from devicetree)
 */
struct sa818_config {
  const struct device *uart;
  struct adc_dt_spec audio_in;
  const struct device *audio_out_dev; /* DAC device (optional) */
  uint8_t audio_out_channel;
  uint8_t audio_out_resolution;

  struct gpio_dt_spec h_l_power;
  struct gpio_dt_spec nptt;
  struct gpio_dt_spec npower_down;
  struct gpio_dt_spec nsquelch;

  uint32_t tx_enable_delay_ms;
  uint32_t rx_settle_time_ms;
};

/**
 * @brief SA818 runtime data
 */
struct sa818_data {
  /* Device state (matches public sa818_status) */
  sa818_device_power device_power;
  sa818_ptt_state ptt_state;
  sa818_power_level power_level;
  bool squelch;

  /* AT Command synchronization */
  struct k_mutex lock;
  struct k_sem at_response_sem;
  char at_response_buf[SA818_AT_RESPONSE_MAX_LEN];
  size_t at_response_len;

  /* Audio state */
  bool audio_rx_enabled;
  bool audio_tx_enabled;
  uint8_t current_volume;

  /* Test tone state */
  struct k_work_delayable test_tone_work;
  const struct device *test_tone_dev; /* Device pointer for work handler */
  bool test_tone_active;
  uint16_t test_tone_freq;
  uint8_t test_tone_amplitude;
  float test_tone_phase;
  int64_t test_tone_end_time;
};

/**
 * @brief Initialize GPIO pins
 *
 * @param cfg Device configuration
 * @return 0 on success, negative errno on failure
 */
int sa818_gpio_init(const struct sa818_config *cfg);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_SA818_SRC_SA818_PRIV_H_ */
