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

#include <errno.h>
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

static void sa818_uart_isr(const struct device *uart, void *user_data) {
  struct sa818_data *data = static_cast<struct sa818_data *>(user_data);
  if (!uart || !data) {
    return;
  }

  while (true) {
    uart_irq_update(uart);
    if (!uart_irq_is_pending(uart)) {
      break;
    }
    if (!uart_irq_rx_ready(uart)) {
      break; /* only RX IRQ is enabled; anything else -> leave, do not spin */
    }

    uint8_t buf[16];
    int n = uart_fifo_read(uart, buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
    if (ring_buf_put(&data->at_rx_rb, buf, n) < static_cast<uint32_t>(n)) {
      data->at_rx_overrun = true;
    }
    k_sem_give(&data->at_rx_sem);
  }
}

int sa818_at_uart_init(const struct device *dev) {
  if (!dev) {
    return -EINVAL;
  }

  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(dev->config);
  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);

  int ret = uart_irq_callback_user_data_set(cfg->uart, sa818_uart_isr, data);
  if (ret != 0) {
    LOG_ERR("SA818 UART IRQ callback registration failed: %d", ret);
    return ret;
  }

  uart_irq_rx_enable(cfg->uart);
  return 0;
}

static void uart_flush_rx(struct sa818_data *data) {
  if (!data) {
    return;
  }
  ring_buf_reset(&data->at_rx_rb);
  k_sem_reset(&data->at_rx_sem);
  data->at_rx_overrun = false;
}

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
static sa818_result uart_read_response(struct sa818_data *data, char *response, size_t response_len, uint32_t timeout_ms) {
  if (!data || !response || response_len == 0) {
    return SA818_ERROR_INVALID_PARAM;
  }

  memset(response, 0, response_len);
  size_t pos = 0;
  const int64_t start = k_uptime_get();

  while (pos < response_len - 1) {
    uint8_t c;
    while (ring_buf_get(&data->at_rx_rb, &c, 1) == 1) {
      if (c == '\n') {
        response[pos] = '\0';
        return SA818_OK;
      }
      if (c == '\r') {
        continue;
      }
      response[pos++] = c;
      if (pos >= response_len - 1) {
        break;
      }
    }

    int32_t remaining = static_cast<int32_t>(timeout_ms) - static_cast<int32_t>(k_uptime_get() - start);
    if (remaining <= 0) {
      LOG_ERR("UART read timeout");
      return SA818_ERROR_TIMEOUT;
    }
    if (k_sem_take(&data->at_rx_sem, K_MSEC(remaining)) != 0) {
      LOG_ERR("UART read timeout");
      return SA818_ERROR_TIMEOUT;
    }
  }

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

  uart_flush_rx(data);

  /* Send command over UART */
  LOG_DBG("TX: %s", cmd);
  sa818_result ret = uart_write_command(cfg->uart, cmd);
  if (ret != SA818_OK) {
    LOG_ERR("Failed to write command: %s", cmd);
    k_mutex_unlock(&data->lock);
    return ret;
  }

  /* Read response from UART */
  ret = uart_read_response(data, response, response_len, timeout_ms);
  if (ret != SA818_OK) {
    LOG_ERR("AT command timeout: %s", cmd);
    k_mutex_unlock(&data->lock);
    return ret;
  }

  if (data->at_rx_overrun) {
    LOG_WRN("SA818 RX ring-buffer overrun; response may be truncated");
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
sa818_result sa818_at_set_group(const struct device *dev, sa818_bandwidth bandwidth, float freq_tx, float freq_rx, sa818_tone_code ctcss_tx,
                                sa818_squelch_level squelch, sa818_tone_code ctcss_rx) {
  char cmd[128];
  char response[SA818_AT_RESPONSE_MAX_LEN];

  /* Validate parameters */
  if (bandwidth != SA818_BW_12_5_KHZ && bandwidth != SA818_BW_25_KHZ) {
    return SA818_ERROR_INVALID_PARAM;
  }
  if (squelch < SA818_SQL_LEVEL_0 || squelch > SA818_SQL_LEVEL_8) {
    return SA818_ERROR_INVALID_PARAM;
  }
  if (ctcss_tx < SA818_TONE_NONE || ctcss_tx > SA818_DCS_523) {
    return SA818_ERROR_INVALID_PARAM;
  }
  if (ctcss_rx < SA818_TONE_NONE || ctcss_rx > SA818_DCS_523) {
    return SA818_ERROR_INVALID_PARAM;
  }

  /* Validate TX frequency (VHF: 134-174 MHz, UHF: 400-480 MHz) */
  if (!((freq_tx >= 134.0f && freq_tx <= 174.0f) || (freq_tx >= 400.0f && freq_tx <= 480.0f))) {
    LOG_ERR("TX freq out of range: %.4f (valid: 134-174 MHz or 400-480 MHz)", (double)freq_tx);
    return SA818_ERROR_INVALID_PARAM;
  }

  /* Validate RX frequency (VHF: 134-174 MHz, UHF: 400-480 MHz) */
  if (!((freq_rx >= 134.0f && freq_rx <= 174.0f) || (freq_rx >= 400.0f && freq_rx <= 480.0f))) {
    LOG_ERR("RX freq out of range: %.4f (valid: 134-174 MHz or 400-480 MHz)", (double)freq_rx);
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

  // Validate filter flags - only bits 0-2 are valid
  if ((filters & ~SA818_FILTER_ALL) != 0) {
    return SA818_ERROR_INVALID_PARAM;
  }

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

/* Bidirectional CTCSS table: code (1..38) <-> Hz string */
struct ctcss_entry {
  sa818_tone_code code;
  float min_freq;
  float max_freq;
  const char *str;
};

static const ctcss_entry CTCSS_TABLE[] = {
    // clang-format off
    {SA818_CTCSS_67_0,  67.0f,  67.1f,  "67.0"},
    {SA818_CTCSS_71_9,  71.8f,  72.0f,  "71.9"},
    {SA818_CTCSS_74_4,  74.3f,  74.5f,  "74.4"},
    {SA818_CTCSS_77_0,  76.9f,  77.1f,  "77.0"},
    {SA818_CTCSS_79_7,  79.6f,  79.8f,  "79.7"},
    {SA818_CTCSS_82_5,  82.4f,  82.6f,  "82.5"},
    {SA818_CTCSS_85_4,  85.3f,  85.5f,  "85.4"},
    {SA818_CTCSS_88_5,  88.4f,  88.6f,  "88.5"},
    {SA818_CTCSS_91_5,  91.4f,  91.6f,  "91.5"},
    {SA818_CTCSS_94_8,  94.7f,  94.9f,  "94.8"},
    {SA818_CTCSS_97_4,  97.3f,  97.5f,  "97.4"},
    {SA818_CTCSS_100_0, 99.9f,  100.1f, "100.0"},
    {SA818_CTCSS_103_5, 103.4f, 103.6f, "103.5"},
    {SA818_CTCSS_107_2, 107.1f, 107.3f, "107.2"},
    {SA818_CTCSS_110_9, 110.8f, 111.0f, "110.9"},
    {SA818_CTCSS_114_8, 114.7f, 114.9f, "114.8"},
    {SA818_CTCSS_118_8, 118.7f, 118.9f, "118.8"},
    {SA818_CTCSS_123_0, 122.9f, 123.1f, "123.0"},
    {SA818_CTCSS_127_3, 127.2f, 127.4f, "127.3"},
    {SA818_CTCSS_131_8, 131.7f, 131.9f, "131.8"},
    {SA818_CTCSS_136_5, 136.4f, 136.6f, "136.5"},
    {SA818_CTCSS_141_3, 141.2f, 141.4f, "141.3"},
    {SA818_CTCSS_146_2, 146.1f, 146.3f, "146.2"},
    {SA818_CTCSS_151_4, 151.3f, 151.5f, "151.4"},
    {SA818_CTCSS_156_7, 156.6f, 156.8f, "156.7"},
    {SA818_CTCSS_162_2, 162.1f, 162.3f, "162.2"},
    {SA818_CTCSS_167_9, 167.8f, 168.0f, "167.9"},
    {SA818_CTCSS_173_8, 173.7f, 173.9f, "173.8"},
    {SA818_CTCSS_179_9, 179.8f, 180.0f, "179.9"},
    {SA818_CTCSS_186_2, 186.1f, 186.3f, "186.2"},
    {SA818_CTCSS_192_8, 192.7f, 192.9f, "192.8"},
    {SA818_CTCSS_203_5, 203.4f, 203.6f, "203.5"},
    {SA818_CTCSS_210_7, 210.6f, 210.8f, "210.7"},
    {SA818_CTCSS_218_1, 218.0f, 218.2f, "218.1"},
    {SA818_CTCSS_225_7, 225.6f, 225.8f, "225.7"},
    {SA818_CTCSS_233_6, 233.5f, 233.7f, "233.6"},
    {SA818_CTCSS_241_8, 241.7f, 241.9f, "241.8"},
    {SA818_CTCSS_250_3, 250.2f, 250.4f, "250.3"},
    // clang-format on
};
static constexpr size_t CTCSS_TABLE_LEN = sizeof(CTCSS_TABLE) / sizeof(CTCSS_TABLE[0]);

/* Bidirectional DCS table: code (39..121) <-> 3-digit string */
struct dcs_entry {
  sa818_tone_code code;
  const char *str;
};

static const dcs_entry DCS_TABLE[] = {
    // clang-format off
    {SA818_DCS_023, "023"}, {SA818_DCS_025, "025"}, {SA818_DCS_026, "026"},
    {SA818_DCS_031, "031"}, {SA818_DCS_032, "032"}, {SA818_DCS_036, "036"},
    {SA818_DCS_043, "043"}, {SA818_DCS_047, "047"}, {SA818_DCS_051, "051"},
    {SA818_DCS_053, "053"}, {SA818_DCS_054, "054"}, {SA818_DCS_065, "065"},
    {SA818_DCS_071, "071"}, {SA818_DCS_072, "072"}, {SA818_DCS_073, "073"},
    {SA818_DCS_074, "074"}, {SA818_DCS_114, "114"}, {SA818_DCS_115, "115"},
    {SA818_DCS_116, "116"}, {SA818_DCS_122, "122"}, {SA818_DCS_125, "125"},
    {SA818_DCS_131, "131"}, {SA818_DCS_132, "132"}, {SA818_DCS_134, "134"},
    {SA818_DCS_143, "143"}, {SA818_DCS_145, "145"}, {SA818_DCS_152, "152"},
    {SA818_DCS_155, "155"}, {SA818_DCS_156, "156"}, {SA818_DCS_162, "162"},
    {SA818_DCS_165, "165"}, {SA818_DCS_172, "172"}, {SA818_DCS_174, "174"},
    {SA818_DCS_205, "205"}, {SA818_DCS_212, "212"}, {SA818_DCS_223, "223"},
    {SA818_DCS_225, "225"}, {SA818_DCS_226, "226"}, {SA818_DCS_243, "243"},
    {SA818_DCS_244, "244"}, {SA818_DCS_245, "245"}, {SA818_DCS_246, "246"},
    {SA818_DCS_251, "251"}, {SA818_DCS_252, "252"}, {SA818_DCS_255, "255"},
    {SA818_DCS_261, "261"}, {SA818_DCS_263, "263"}, {SA818_DCS_265, "265"},
    {SA818_DCS_266, "266"}, {SA818_DCS_271, "271"}, {SA818_DCS_274, "274"},
    {SA818_DCS_306, "306"}, {SA818_DCS_311, "311"}, {SA818_DCS_315, "315"},
    {SA818_DCS_325, "325"}, {SA818_DCS_331, "331"}, {SA818_DCS_332, "332"},
    {SA818_DCS_343, "343"}, {SA818_DCS_346, "346"}, {SA818_DCS_351, "351"},
    {SA818_DCS_356, "356"}, {SA818_DCS_364, "364"}, {SA818_DCS_365, "365"},
    {SA818_DCS_371, "371"}, {SA818_DCS_411, "411"}, {SA818_DCS_412, "412"},
    {SA818_DCS_413, "413"}, {SA818_DCS_423, "423"}, {SA818_DCS_431, "431"},
    {SA818_DCS_432, "432"}, {SA818_DCS_445, "445"}, {SA818_DCS_446, "446"},
    {SA818_DCS_452, "452"}, {SA818_DCS_454, "454"}, {SA818_DCS_455, "455"},
    {SA818_DCS_462, "462"}, {SA818_DCS_464, "464"}, {SA818_DCS_465, "465"},
    {SA818_DCS_466, "466"}, {SA818_DCS_503, "503"}, {SA818_DCS_506, "506"},
    {SA818_DCS_516, "516"}, {SA818_DCS_523, "523"},
    // clang-format on
};
static constexpr size_t DCS_TABLE_LEN = sizeof(DCS_TABLE) / sizeof(DCS_TABLE[0]);

/**
 * @brief Check if a string is purely numeric (no sign prefix allowed for DCS).
 */
static bool is_all_digits(const char *s) {
  if (!s || *s == '\0') {
    return false;
  }
  for (; *s != '\0'; ++s) {
    if (*s < '0' || *s > '9') {
      return false;
    }
  }
  return true;
}

sa818_tone_code sa818_at_parse_tone(const char *s) {
  if (!s) {
    return SA818_TONE_NONE;
  }

  /* "none" / "off" */
  if (!strcmp(s, "none") || !strcmp(s, "off")) {
    return SA818_TONE_NONE;
  }

  /* Try to parse as CTCSS frequency (e.g. "67.0") -- require the WHOLE string to be a
   * float, so partial garbage like "67.0junk" is NOT accepted as a tone. */
  char *end = nullptr;
  float freq = strtof(s, &end);
  if (end != s && *end == '\0' && freq > 60.0f && freq < 260.0f) {
    for (size_t i = 0; i < CTCSS_TABLE_LEN; ++i) {
      if (freq >= CTCSS_TABLE[i].min_freq && freq <= CTCSS_TABLE[i].max_freq) {
        return CTCSS_TABLE[i].code;
      }
    }
  }

  /* DCS: a purely-numeric string of exactly 3 digits (e.g. "023") - look up in table */
  if (is_all_digits(s) && strlen(s) == 3) {
    for (size_t i = 0; i < DCS_TABLE_LEN; ++i) {
      if (!strcmp(s, DCS_TABLE[i].str)) {
        return DCS_TABLE[i].code;
      }
    }
  }

  /* Fallback: numeric code 0..121 (e.g. from shell numeric input) */
  if (s[0] != '\0') {
    const char *p = s;
    if (*p == '+' || *p == '-') {
      ++p;
    }
    if (*p != '\0') {
      bool is_numeric = true;
      const char *q = p;
      while (*q != '\0') {
        if (*q < '0' || *q > '9') {
          is_numeric = false;
          break;
        }
        ++q;
      }
      if (is_numeric) {
        int value = atoi(s);
        if (value >= 0 && value <= 121) {
          return static_cast<sa818_tone_code>(value);
        }
      }
    }
  }

  return SA818_TONE_NONE;
}

int sa818_at_tone_to_str(sa818_tone_code code, char *buf, size_t len) {
  if (!buf || len == 0) {
    return -1;
  }

  /* None */
  if (code == SA818_TONE_NONE) {
    int n = snprintf(buf, len, "none");
    return (n >= 0 && (size_t)n < len) ? n : -1;
  }

  /* CTCSS range 1..38 */
  if (code >= SA818_CTCSS_67_0 && code <= SA818_CTCSS_250_3) {
    for (size_t i = 0; i < CTCSS_TABLE_LEN; ++i) {
      if (CTCSS_TABLE[i].code == code) {
        int n = snprintf(buf, len, "%s", CTCSS_TABLE[i].str);
        return (n >= 0 && (size_t)n < len) ? n : -1;
      }
    }
  }

  /* DCS range 39..121 */
  if (code >= SA818_DCS_023 && code <= SA818_DCS_523) {
    for (size_t i = 0; i < DCS_TABLE_LEN; ++i) {
      if (DCS_TABLE[i].code == code) {
        int n = snprintf(buf, len, "%s", DCS_TABLE[i].str);
        return (n >= 0 && (size_t)n < len) ? n : -1;
      }
    }
  }

  return -1;
}
