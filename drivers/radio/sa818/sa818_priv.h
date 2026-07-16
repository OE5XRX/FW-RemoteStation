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
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Private structures and definitions shared between SA818 modules
 */

/* AT Command timeouts and constants.
 * The SA818 answers AT commands within ~1 s; 2000 ms is ample. (The 5000 ms
 * value was bumped during bring-up while chasing the swapped-PD-GPIO issue and
 * only served to blow the native_sim test wall-clock, since commands issued
 * without the AT simulator attached block for the full timeout.) */
#define SA818_AT_TIMEOUT_MS 2000
#define SA818_AT_RESPONSE_MAX_LEN 128
#define SA818_UART_BAUDRATE 9600
/* RX ring buffer: > SA818_AT_RESPONSE_MAX_LEN (128) with burst headroom */
#define SA818_AT_RX_RB_SIZE 256

/* Initialization delays */
#define SA818_INIT_DELAY_MS 10
/* SA818 needs a few hundred ms after PD is released before it accepts AT
 * commands; 500 ms is a safe margin. (The earlier 1500 ms was a red herring
 * from debugging the swapped PD GPIO, which is the real reason the module
 * used to stay silent.) */
#define SA818_POWER_ON_DELAY_MS 500

/**
 * @brief SA818 device configuration (from devicetree)
 */
struct sa818_config {
  const struct device *uart;

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
  struct ring_buf at_rx_rb; /* ISR -> reader byte stream */
  uint8_t at_rx_rb_buf[SA818_AT_RX_RB_SIZE];
  struct k_sem at_rx_sem; /* "data available", binary (max count 1) */
  bool at_rx_overrun;     /* ISR sets on ring-buffer overflow */

  /* SA818 AT volume setting (AT+DMOSETVOLUME), reported in sa818_status */
  uint8_t current_volume;
};

/**
 * @brief Initialize GPIO pins
 *
 * @param cfg Device configuration
 * @return 0 on success, negative errno on failure
 */
int sa818_gpio_init(const struct sa818_config *cfg);

int sa818_at_uart_init(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_SA818_SRC_SA818_PRIV_H_ */
