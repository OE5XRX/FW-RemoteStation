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
#include <string_view>
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
 * @return SA818_OK on success, SA818_ERROR_INVALID_PARAM if uart is NULL or cmd is empty
 */
static sa818_result uart_write_command(const struct device *uart, std::string_view cmd) {
  if (!uart || cmd.empty()) {
    return SA818_ERROR_INVALID_PARAM;
  }

  /* Send command characters */
  for (char c : cmd) {
    uart_poll_out(uart, static_cast<unsigned char>(c));
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
    current_time = k_uptime_get();
    int64_t elapsed = current_time - start_time;
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
 * @brief Establish connection handshake with SA818 module
 *
 * AT+DMOCONNECT command verifies UART communication with the radio module.
 * Expected response: +DMOCONNECT:0
 */
sa818_result sa818_at_connect(const struct device *dev) {
  char response[SA818_AT_RESPONSE_MAX_LEN];

  sa818_result ret = sa818_at_send_command(dev, "AT+DMOCONNECT", response, sizeof(response), SA818_AT_TIMEOUT_MS);
  if (ret != SA818_OK) {
    return ret;
  }

  /* Check for OK response */
  if (strstr(response, "+DMOCONNECT:0") == NULL) {
    LOG_ERR("Connect failed: %s", response);
    return SA818_ERROR_AT_COMMAND;
  }

  LOG_INF("SA818 connected successfully");
  return SA818_OK;
}

/**
 * @brief Set radio group (frequency, CTCSS, squelch)
 *
 * AT+DMOSETGROUP=BW,TXF,RXF,TXCCS,SQ,RXCCS
 * Example: AT+DMOSETGROUP=0,145.5000,145.5000,0000,4,0000
 */
sa818_result sa818_at_set_group(const struct device *dev, sa818_bandwidth bandwidth, float freq_tx, float freq_rx, sa818_tone_code ctcss_tx, sa818_squelch_level squelch,
                                sa818_tone_code ctcss_rx) {
  char cmd[128];
  char response[SA818_AT_RESPONSE_MAX_LEN];

  /* Validate parameters */
  if (squelch < SA818_SQL_LEVEL_0 || squelch > SA818_SQL_LEVEL_8) {
    return SA818_ERROR_INVALID_PARAM;
  }
  if (ctcss_tx < SA818_TONE_NONE || ctcss_tx > SA818_DCS_523) {
    return SA818_ERROR_INVALID_PARAM;
  }
  if (ctcss_rx < SA818_TONE_NONE || ctcss_rx > SA818_DCS_523) {
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

  LOG_INF("Group configured: TX=%.4f RX=%.4f SQ=%d", (double)freq_tx, (double)freq_rx, static_cast<int>(squelch));
  return SA818_OK;
}

/**
 * @brief Set volume level
 *
 * AT+DMOSETVOLUME=N where N is 1-8
 */
sa818_result sa818_at_set_volume(const struct device *dev, sa818_volume_level volume) {
  char cmd[32];
  char response[SA818_AT_RESPONSE_MAX_LEN];

  if (volume < SA818_VOLUME_1 || volume > SA818_VOLUME_8) {
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
  data->current_volume = static_cast<uint8_t>(volume);

  LOG_INF("Volume set to %d", static_cast<int>(volume));
  return SA818_OK;
}

/**
 * @brief Configure audio filters
 *
 * AT+SETFILTER=PRE,HPF,LPF where each is 0 or 1
 */
sa818_result sa818_at_set_filters(const struct device *dev, sa818_filter_flags filters) {
  char cmd[64];
  char response[SA818_AT_RESPONSE_MAX_LEN];

  bool pre_emphasis = (filters & SA818_FILTER_PRE_EMPHASIS) != 0;
  bool high_pass = (filters & SA818_FILTER_HIGH_PASS) != 0;
  bool low_pass = (filters & SA818_FILTER_LOW_PASS) != 0;

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

/**
 * @brief Read firmware version
 *
 * AT+VERSION command returns firmware version string
 */
sa818_result sa818_at_read_version(const struct device *dev, char *version, size_t version_len) {
  char response[SA818_AT_RESPONSE_MAX_LEN];

  if (!version || version_len == 0) {
    return SA818_ERROR_INVALID_PARAM;
  }

  sa818_result ret = sa818_at_send_command(dev, "AT+VERSION", response, sizeof(response), SA818_AT_TIMEOUT_MS);
  if (ret != SA818_OK) {
    return ret;
  }

  /* Copy version string to output buffer */
  strncpy(version, response, version_len - 1);
  version[version_len - 1] = '\0';
  
  LOG_INF("Version: %s", version);

  return SA818_OK;
}
