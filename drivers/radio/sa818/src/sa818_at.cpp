/**
 * @file sa818_at.cpp
 * @brief SA818 AT Command Implementation
 *
 * Implements UART-based AT command protocol for SA818 configuration.
 * Handles command transmission, response parsing, and error handling.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#include "sa818_priv.h"

#include <sa818/sa818_at.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sa818_at, LOG_LEVEL_DBG);

/**
 * @brief Send raw AT command and wait for response
 *
 * This is the core AT command handler. It sends a command string
 * over UART and waits for a response with timeout.
 */
int sa818_at_send_command(const struct device *dev, const char *cmd, char *response, size_t response_len, uint32_t timeout_ms) {
  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(dev->config);
  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);

  if (!cmd) {
    return -EINVAL;
  }

  k_mutex_lock(&data->lock, K_FOREVER);

  /* Clear previous response */
  data->at_response_len = 0;
  memset(data->at_response_buf, 0, sizeof(data->at_response_buf));

  /* Send command over UART */
  LOG_DBG("TX: %s", cmd);
  for (size_t i = 0; i < strlen(cmd); i++) {
    uart_poll_out(cfg->uart, cmd[i]);
  }
  uart_poll_out(cfg->uart, '\r'); // AT commands end with CR
  uart_poll_out(cfg->uart, '\n'); // Some modules need CRLF

  /* Wait for response with timeout */
  int ret = k_sem_take(&data->at_response_sem, K_MSEC(timeout_ms));
  if (ret != 0) {
    LOG_ERR("AT command timeout: %s", cmd);
    k_mutex_unlock(&data->lock);
    return -ETIMEDOUT;
  }

  /* Copy response if buffer provided */
  if (response && response_len > 0) {
    size_t copy_len = MIN(data->at_response_len, response_len - 1);
    memcpy(response, data->at_response_buf, copy_len);
    response[copy_len] = '\0';
  }

  LOG_DBG("RX: %s", data->at_response_buf);
  k_mutex_unlock(&data->lock);

  return 0;
}

/**
 * @brief Set radio group (frequency, CTCSS, squelch)
 *
 * AT+DMOSETGROUP=BW,TXF,RXF,TXCCS,SQ,RXCCS
 * Example: AT+DMOSETGROUP=0,145.5000,145.5000,0000,4,0000
 */
int sa818_at_set_group(const struct device *dev, uint8_t bandwidth, float freq_tx, float freq_rx, uint16_t ctcss_tx, uint8_t squelch, uint16_t ctcss_rx) {
  char cmd[128];
  char response[SA818_AT_RESPONSE_MAX_LEN];

  /* Validate parameters */
  if (squelch > 8) {
    return -EINVAL;
  }
  if (freq_tx < 134.0f || freq_tx > 174.0f) {
    LOG_ERR("TX freq out of range: %.4f", (double)freq_tx);
    return -EINVAL;
  }

  /* Format command */
  snprintf(cmd, sizeof(cmd), "AT+DMOSETGROUP=%d,%.4f,%.4f,%04d,%d,%04d", bandwidth, (double)freq_tx, (double)freq_rx, ctcss_tx, squelch, ctcss_rx);

  int ret = sa818_at_send_command(dev, cmd, response, sizeof(response), SA818_AT_TIMEOUT_MS);
  if (ret != 0) {
    return ret;
  }

  /* Check for OK response */
  if (strstr(response, "+DMOSETGROUP:0") == NULL) {
    LOG_ERR("Set group failed: %s", response);
    return -EIO;
  }

  LOG_INF("Group configured: TX=%.4f RX=%.4f SQ=%d", (double)freq_tx, (double)freq_rx, squelch);
  return 0;
}

/**
 * @brief Set volume level
 *
 * AT+DMOSETVOLUME=N where N is 1-8
 */
int sa818_at_set_volume(const struct device *dev, uint8_t volume) {
  char cmd[32];
  char response[SA818_AT_RESPONSE_MAX_LEN];

  if (volume < 1 || volume > 8) {
    return -EINVAL;
  }

  snprintf(cmd, sizeof(cmd), "AT+DMOSETVOLUME=%d", volume);

  int ret = sa818_at_send_command(dev, cmd, response, sizeof(response), SA818_AT_TIMEOUT_MS);
  if (ret != 0) {
    return ret;
  }

  if (strstr(response, "+DMOSETVOLUME:0") == NULL) {
    LOG_ERR("Set volume failed: %s", response);
    return -EIO;
  }

  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);
  data->current_volume = volume;

  LOG_INF("Volume set to %d", volume);
  return 0;
}

/**
 * @brief Configure audio filters
 *
 * AT+SETFILTER=PRE,HPF,LPF where each is 0 or 1
 */
int sa818_at_set_filters(const struct device *dev, bool pre_emphasis, bool high_pass, bool low_pass) {
  char cmd[64];
  char response[SA818_AT_RESPONSE_MAX_LEN];

  snprintf(cmd, sizeof(cmd), "AT+SETFILTER=%d,%d,%d", pre_emphasis ? 1 : 0, high_pass ? 1 : 0, low_pass ? 1 : 0);

  int ret = sa818_at_send_command(dev, cmd, response, sizeof(response), SA818_AT_TIMEOUT_MS);
  if (ret != 0) {
    return ret;
  }

  if (strstr(response, "+DMOSETFILTER:0") == NULL) {
    LOG_ERR("Set filters failed: %s", response);
    return -EIO;
  }

  LOG_INF("Filters: PRE=%d HPF=%d LPF=%d", pre_emphasis, high_pass, low_pass);
  return 0;
}

/**
 * @brief Read RSSI (signal strength)
 *
 * RSSI? command returns signal strength value
 */
int sa818_at_read_rssi(const struct device *dev, uint8_t *rssi) {
  char response[SA818_AT_RESPONSE_MAX_LEN];

  if (!rssi) {
    return -EINVAL;
  }

  int ret = sa818_at_send_command(dev, "RSSI?", response, sizeof(response), SA818_AT_TIMEOUT_MS);
  if (ret != 0) {
    return ret;
  }

  /* Parse RSSI value from response */
  /* Expected format: RSSI=xxx */
  char *rssi_str = strstr(response, "RSSI=");
  if (!rssi_str) {
    LOG_ERR("Invalid RSSI response: %s", response);
    return -EIO;
  }

  *rssi = atoi(rssi_str + 5);
  LOG_DBG("RSSI: %d", *rssi);

  return 0;
}
