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
 * @brief Write AT command to UART
 *
 * Sends the command string character by character using uart_poll_out(),
 * followed by CR+LF line terminator.
 *
 * @param uart UART device
 * @param cmd Command string to send
 * @return SA818_OK on success, SA818_ERROR_INVALID_PARAM if cmd is NULL
 */
static sa818_result uart_write_command(const struct device *uart, const char *cmd) {
  if (!uart || !cmd) {
    return SA818_ERROR_INVALID_PARAM;
  }

  /* Send command characters */
  for (size_t i = 0; i < strlen(cmd); i++) {
    uart_poll_out(uart, cmd[i]);
  }

  /* Send CR+LF terminator */
  uart_poll_out(uart, '\r');
  uart_poll_out(uart, '\n');

  return SA818_OK;
}

/**
 * @brief Read UART response with timeout
 *
 * Reads characters from UART using uart_poll_in() until newline is received
 * or timeout expires. Implements timeout using k_uptime_get() and k_uptime_delta().
 *
 * @param uart UART device
 * @param response Buffer to store response
 * @param response_len Size of response buffer
 * @param timeout_ms Timeout in milliseconds
 * @return SA818_OK on success, SA818_ERROR_TIMEOUT on timeout, SA818_ERROR_INVALID_PARAM on invalid params
 */
static sa818_result uart_read_response(const struct device *uart, char *response, size_t response_len, uint32_t timeout_ms) {
  if (!uart || !response || response_len == 0) {
    return SA818_ERROR_INVALID_PARAM;
  }

  const int64_t start_time = k_uptime_get();
  int64_t current_time = start_time;
  size_t pos = 0;

  /* Clear response buffer */
  memset(response, 0, response_len);

  while (pos < response_len - 1) {
    /* Check timeout */
    current_time = start_time;
    int64_t elapsed = k_uptime_delta(&current_time);
    if (elapsed >= timeout_ms) {
      LOG_ERR("UART read timeout after %lld ms", elapsed);
      return SA818_ERROR_TIMEOUT;
    }

    /* Try to read one character */
    unsigned char c;
    int ret = uart_poll_in(uart, &c);

    if (ret == 0) {
      /* Character received */
      if (c == '\n') {
        /* Newline marks end of response */
        response[pos] = '\0';
        return SA818_OK;
      } else if (c == '\r') {
        /* Skip carriage return */
        continue;
      } else {
        /* Store character */
        response[pos++] = c;
      }
    } else {
      /* No data available, sleep briefly to avoid busy-wait */
      k_sleep(K_MSEC(1));
    }
  }

  /* Buffer full without receiving newline */
  response[response_len - 1] = '\0';
  return SA818_OK;
}

/**
 * @brief Send raw AT command and wait for response
 *
 * This is the core AT command handler. It sends a command string
 * over UART and waits for a response with timeout.
 */
sa818_result sa818_at_send_command(const struct device *dev, const char *cmd, char *response, size_t response_len, uint32_t timeout_ms) {
  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(dev->config);
  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);

  if (!cmd) {
    return SA818_ERROR_INVALID_PARAM;
  }

  k_mutex_lock(&data->lock, K_FOREVER);

  /* Send command over UART */
  LOG_DBG("TX: %s", cmd);
  sa818_result ret = uart_write_command(cfg->uart, cmd);
  if (ret != SA818_OK) {
    LOG_ERR("Failed to write command: %s", cmd);
    k_mutex_unlock(&data->lock);
    return ret;
  }

  /* Read response from UART */
  ret = uart_read_response(cfg->uart, response, response_len, timeout_ms);
  if (ret != SA818_OK) {
    LOG_ERR("AT command timeout: %s", cmd);
    k_mutex_unlock(&data->lock);
    return ret;
  }

  LOG_DBG("RX: %s", response);

  k_mutex_unlock(&data->lock);

  return SA818_OK;
}

/**
 * @brief Set radio group (frequency, CTCSS, squelch)
 *
 * AT+DMOSETGROUP=BW,TXF,RXF,TXCCS,SQ,RXCCS
 * Example: AT+DMOSETGROUP=0,145.5000,145.5000,0000,4,0000
 */
sa818_result sa818_at_set_group(const struct device *dev, uint8_t bandwidth, float freq_tx, float freq_rx, uint16_t ctcss_tx, uint8_t squelch,
                                uint16_t ctcss_rx) {
  char cmd[128];
  char response[SA818_AT_RESPONSE_MAX_LEN];

  /* Validate parameters */
  if (squelch > 8) {
    return SA818_ERROR_INVALID_PARAM;
  }
  if (freq_tx < 134.0f || freq_tx > 174.0f) {
    LOG_ERR("TX freq out of range: %.4f", (double)freq_tx);
    return SA818_ERROR_INVALID_PARAM;
  }

  /* Format command */
  snprintf(cmd, sizeof(cmd), "AT+DMOSETGROUP=%d,%.4f,%.4f,%04d,%d,%04d", bandwidth, (double)freq_tx, (double)freq_rx, ctcss_tx, squelch, ctcss_rx);

  sa818_result ret = sa818_at_send_command(dev, cmd, response, sizeof(response), SA818_AT_TIMEOUT_MS);
  if (ret != SA818_OK) {
    return ret;
  }

  /* Check for OK response */
  if (strstr(response, "+DMOSETGROUP:0") == NULL) {
    LOG_ERR("Set group failed: %s", response);
    return SA818_ERROR_AT_COMMAND;
  }

  LOG_INF("Group configured: TX=%.4f RX=%.4f SQ=%d", (double)freq_tx, (double)freq_rx, squelch);
  return SA818_OK;
}

/**
 * @brief Set volume level
 *
 * AT+DMOSETVOLUME=N where N is 1-8
 */
sa818_result sa818_at_set_volume(const struct device *dev, uint8_t volume) {
  char cmd[32];
  char response[SA818_AT_RESPONSE_MAX_LEN];

  if (volume < 1 || volume > 8) {
    return SA818_ERROR_INVALID_PARAM;
  }

  snprintf(cmd, sizeof(cmd), "AT+DMOSETVOLUME=%d", volume);

  sa818_result ret = sa818_at_send_command(dev, cmd, response, sizeof(response), SA818_AT_TIMEOUT_MS);
  if (ret != SA818_OK) {
    return ret;
  }

  if (strstr(response, "+DMOSETVOLUME:0") == NULL) {
    LOG_ERR("Set volume failed: %s", response);
    return SA818_ERROR_AT_COMMAND;
  }

  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);
  data->current_volume = volume;

  LOG_INF("Volume set to %d", volume);
  return SA818_OK;
}

/**
 * @brief Configure audio filters
 *
 * AT+SETFILTER=PRE,HPF,LPF where each is 0 or 1
 */
sa818_result sa818_at_set_filters(const struct device *dev, bool pre_emphasis, bool high_pass, bool low_pass) {
  char cmd[64];
  char response[SA818_AT_RESPONSE_MAX_LEN];

  snprintf(cmd, sizeof(cmd), "AT+SETFILTER=%d,%d,%d", pre_emphasis ? 1 : 0, high_pass ? 1 : 0, low_pass ? 1 : 0);

  sa818_result ret = sa818_at_send_command(dev, cmd, response, sizeof(response), SA818_AT_TIMEOUT_MS);
  if (ret != SA818_OK) {
    return ret;
  }

  if (strstr(response, "+DMOSETFILTER:0") == NULL) {
    LOG_ERR("Set filters failed: %s", response);
    return SA818_ERROR_AT_COMMAND;
  }

  LOG_INF("Filters: PRE=%d HPF=%d LPF=%d", pre_emphasis, high_pass, low_pass);
  return SA818_OK;
}

/**
 * @brief Read RSSI (signal strength)
 *
 * RSSI? command returns signal strength value
 */
sa818_result sa818_at_read_rssi(const struct device *dev, uint8_t *rssi) {
  char response[SA818_AT_RESPONSE_MAX_LEN];

  if (!rssi) {
    return SA818_ERROR_INVALID_PARAM;
  }

  sa818_result ret = sa818_at_send_command(dev, "RSSI?", response, sizeof(response), SA818_AT_TIMEOUT_MS);
  if (ret != SA818_OK) {
    return ret;
  }

  /* Parse RSSI value from response */
  /* Expected format: RSSI=xxx */
  char *rssi_str = strstr(response, "RSSI=");
  if (!rssi_str) {
    LOG_ERR("Invalid RSSI response: %s", response);
    return SA818_ERROR_AT_COMMAND;
  }

  *rssi = atoi(rssi_str + 5);
  LOG_DBG("RSSI: %d", *rssi);

  return SA818_OK;
}
