/**
 * @file sa818_shell.cpp
 * @brief SA818 Shell Command Interface
 *
 * Provides interactive shell commands for testing and controlling
 * SA818 radio module via Zephyr shell interface.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#ifdef CONFIG_SA818_SHELL

#include <sa818/sa818.h>
#include <sa818/sa818_at.h>
#include <sa818/sa818_audio.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/shell/shell.h>

static const struct device *sa818_dev(void) {
  return DEVICE_DT_GET_OR_NULL(DT_NODELABEL(sa818));
}

static int cmd_sa818_status(const struct shell *shell, size_t, char **) {
  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    shell_print(shell, "sa818 not ready");
    return 0;
  }

  sa818_status st = sa818_get_status(dev);
  shell_print(shell, "powered=%d ptt=%d high_power=%d squelch=%d volume=%d", st.device_power, st.ptt_state, st.power_level, st.squelch_state, st.volume);
  return 0;
}

static int cmd_sa818_power(const struct shell *shell, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(shell, "usage: sa818 power on|off");
    return -EINVAL;
  }

  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    shell_error(shell, "sa818 not ready");
    return -ENODEV;
  }

  if (!strcmp(argv[1], "on")) {
    sa818_result ret = sa818_set_power(dev, SA818_DEVICE_ON);
    if (ret != SA818_OK) {
      shell_error(shell, "Failed to power on: %d", ret);
      return ret;
    }
  } else if (!strcmp(argv[1], "off")) {
    sa818_result ret = sa818_set_power(dev, SA818_DEVICE_OFF);
    if (ret != SA818_OK) {
      shell_error(shell, "Failed to power off: %d", ret);
      return ret;
    }
  } else {
    shell_error(shell, "invalid arg");
    return -EINVAL;
  }
  return 0;
}

static int cmd_sa818_ptt(const struct shell *shell, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(shell, "usage: sa818 ptt on|off");
    return -EINVAL;
  }

  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    shell_error(shell, "sa818 not ready");
    return -ENODEV;
  }

  if (!strcmp(argv[1], "on")) {
    sa818_result ret = sa818_set_ptt(dev, SA818_PTT_ON);
    if (ret != SA818_OK) {
      shell_error(shell, "Failed to enable PTT: %d", ret);
      return ret;
    }
  } else if (!strcmp(argv[1], "off")) {
    sa818_result ret = sa818_set_ptt(dev, SA818_PTT_OFF);
    if (ret != SA818_OK) {
      shell_error(shell, "Failed to disable PTT: %d", ret);
      return ret;
    }
  } else {
    shell_error(shell, "invalid arg");
    return -EINVAL;
  }
  return 0;
}

static int cmd_sa818_powerlevel(const struct shell *shell, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(shell, "usage: sa818 powerlevel high|low");
    return -EINVAL;
  }

  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    shell_error(shell, "sa818 not ready");
    return -ENODEV;
  }

  if (!strcmp(argv[1], "high")) {
    sa818_result ret = sa818_set_power_level(dev, SA818_POWER_HIGH);
    if (ret != SA818_OK) {
      shell_error(shell, "Failed to set power level: %d", ret);
      return ret;
    }
  } else if (!strcmp(argv[1], "low")) {
    sa818_result ret = sa818_set_power_level(dev, SA818_POWER_LOW);
    if (ret != SA818_OK) {
      shell_error(shell, "Failed to set power level: %d", ret);
      return ret;
    }
  } else {
    shell_error(shell, "invalid arg");
    return -EINVAL;
  }
  return 0;
}

static int cmd_sa818_squelch_sim(const struct shell *shell, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(shell, "usage: sa818 squelch_sim open|closed");
    return -EINVAL;
  }

  // Get GPIO emulator device
  const struct device *gpio_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(gpio_sa818));
  if (!gpio_dev || !device_is_ready(gpio_dev)) {
    shell_error(shell, "gpio_sa818 emulator not ready");
    return -ENODEV;
  }

  // nsquelch is on pin 3, active LOW
  // Physical 0 = squelch closed (carrier detected)
  // Physical 1 = squelch open (no carrier)
  int pin_value;
  if (!strcmp(argv[1], "open")) {
    pin_value = 1; // No carrier
    shell_print(shell, "Simulating squelch OPEN (no carrier)");
  } else if (!strcmp(argv[1], "closed")) {
    pin_value = 0; // Carrier detected
    shell_print(shell, "Simulating squelch CLOSED (carrier detected)");
  } else {
    shell_error(shell, "invalid arg - use 'open' or 'closed'");
    return -EINVAL;
  }

  int ret = gpio_emul_input_set(gpio_dev, 3, pin_value);
  if (ret != 0) {
    shell_error(shell, "Failed to set emulator input: %d", ret);
    return ret;
  }

  return 0;
}

/* AT Command Shell Commands */
static int cmd_sa818_at_connect(const struct shell *shell, size_t argc, char **argv) {
  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    shell_error(shell, "sa818 not ready");
    return -ENODEV;
  }

  sa818_result ret = sa818_at_connect(dev);
  if (ret != SA818_OK) {
    shell_error(shell, "AT+DMOCONNECT failed: %d", ret);
    return ret;
  }

  shell_print(shell, "SA818 connection handshake successful");
  return 0;
}

static int cmd_sa818_at_volume(const struct shell *shell, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(shell, "usage: sa818 at volume <1-8>");
    return -EINVAL;
  }

  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    shell_error(shell, "sa818 not ready");
    return -ENODEV;
  }

  int volume = atoi(argv[1]);
  if (volume < 1 || volume > 8) {
    shell_error(shell, "volume must be 1-8");
    return -EINVAL;
  }

  sa818_result ret = sa818_at_set_volume(dev, static_cast<sa818_volume_level>(volume));
  if (ret != SA818_OK) {
    shell_error(shell, "AT command failed: %d", ret);
    return ret;
  }

  shell_print(shell, "Volume set to %d", volume);
  return 0;
}

/**
 * @brief Parse bandwidth string to enum
 * Accepts: "narrow", "12.5", "wide", "25", or numeric value (0 or 1)
 */
static sa818_bandwidth parse_bandwidth(const char *str) {
  if (!strcmp(str, "narrow") || !strcmp(str, "12.5")) {
    return SA818_BW_12_5_KHZ;
  } else if (!strcmp(str, "wide") || !strcmp(str, "25")) {
    return SA818_BW_25_KHZ;
  }

  // Validate numeric input
  if (str == nullptr || *str == '\0') {
    return SA818_BW_12_5_KHZ; // Default to narrow
  }

  // Check if string is numeric
  const char *p = str;
  if (*p == '+' || *p == '-') {
    ++p;
  }

  if (*p == '\0') {
    return SA818_BW_12_5_KHZ; // Only a sign, default to narrow
  }

  while (*p != '\0') {
    if (*p < '0' || *p > '9') {
      return SA818_BW_12_5_KHZ; // Non-numeric, default to narrow
    }
    ++p;
  }

  // Valid numeric string - parse it
  int value = atoi(str);
  if (value == 0 || value == 1) {
    return static_cast<sa818_bandwidth>(value);
  }

  return SA818_BW_12_5_KHZ; // Out of range, default to narrow
}

static int cmd_sa818_at_group(const struct shell *shell, size_t argc, char **argv) {
  if (argc < 7) {
    shell_error(shell, "usage: sa818 at group <bw> <tx_freq> <rx_freq> <tx_tone> <squelch> <rx_tone>");
    shell_error(shell, "  bw: narrow/12.5 or wide/25");
    shell_error(shell, "  tone: none/off, CTCSS frequency (67.0-250.3), or numeric code");
    shell_error(shell, "example: sa818 at group narrow 145.500 145.500 none 4 none");
    shell_error(shell, "example: sa818 at group wide 145.500 145.500 67.0 4 67.0");
    return -EINVAL;
  }

  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    shell_error(shell, "sa818 not ready");
    return -ENODEV;
  }

  sa818_bandwidth bw = parse_bandwidth(argv[1]);
  float tx_freq = atof(argv[2]);
  float rx_freq = atof(argv[3]);
  sa818_tone_code tx_tone = sa818_at_parse_tone(argv[4]);
  int squelch = atoi(argv[5]);
  sa818_tone_code rx_tone = sa818_at_parse_tone(argv[6]);

  if (squelch < 0 || squelch > 8) {
    shell_error(shell, "invalid squelch level: %d (valid range: 0-8)", squelch);
    return -EINVAL;
  }

  sa818_result ret = sa818_at_set_group(dev, bw, tx_freq, rx_freq, tx_tone, static_cast<sa818_squelch_level>(squelch), rx_tone);
  if (ret != SA818_OK) {
    shell_error(shell, "AT command failed: %d", ret);
    return ret;
  }

  shell_print(shell, "Group configured: TX=%.3f RX=%.3f SQ=%d", (double)tx_freq, (double)rx_freq, squelch);
  return 0;
}

static int cmd_sa818_at_filters(const struct shell *shell, size_t argc, char **argv) {
  if (argc < 4) {
    shell_error(shell, "usage: sa818 at filters <pre> <hpf> <lpf>");
    shell_error(shell, "  Each filter: 0=off, 1=on");
    shell_error(shell, "example: sa818 at filters 1 1 1  (all enabled)");
    return -EINVAL;
  }

  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    shell_error(shell, "sa818 not ready");
    return -ENODEV;
  }

  bool pre = atoi(argv[1]) != 0;
  bool hpf = atoi(argv[2]) != 0;
  bool lpf = atoi(argv[3]) != 0;

  // Build filter flags
  sa818_filter_flags filters = SA818_FILTER_NONE;
  if (pre) {
    filters = static_cast<sa818_filter_flags>(filters | SA818_FILTER_PRE_EMPHASIS);
  }
  if (hpf) {
    filters = static_cast<sa818_filter_flags>(filters | SA818_FILTER_HIGH_PASS);
  }
  if (lpf) {
    filters = static_cast<sa818_filter_flags>(filters | SA818_FILTER_LOW_PASS);
  }

  sa818_result ret = sa818_at_set_filters(dev, filters);
  if (ret != SA818_OK) {
    shell_error(shell, "AT command failed: %d", ret);
    return ret;
  }

  shell_print(shell, "Filters: PRE=%d HPF=%d LPF=%d", pre, hpf, lpf);
  return 0;
}

static int cmd_sa818_at_rssi(const struct shell *shell, size_t argc, char **argv) {
  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    shell_error(shell, "sa818 not ready");
    return -ENODEV;
  }

  uint8_t rssi = 0;
  sa818_result ret = sa818_at_read_rssi(dev, &rssi);
  if (ret != SA818_OK) {
    shell_error(shell, "AT command failed: %d", ret);
    return ret;
  }

  shell_print(shell, "RSSI: %d", rssi);
  return 0;
}

static int cmd_sa818_at_version(const struct shell *shell, size_t argc, char **argv) {
  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    shell_error(shell, "sa818 not ready");
    return -ENODEV;
  }

  char version[64];
  sa818_result ret = sa818_at_read_version(dev, version, sizeof(version));
  if (ret != SA818_OK) {
    shell_error(shell, "AT command failed: %d", ret);
    return ret;
  }

  shell_print(shell, "Version: %s", version);
  return 0;
}

/* Test Tone Commands */
static int cmd_sa818_test_tone(const struct shell *shell, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(shell, "usage: sa818 test tone <freq_hz> [duration_ms] [amplitude]");
    shell_error(shell, "  freq_hz: 100-3000 Hz");
    shell_error(shell, "  duration_ms: 0 = continuous (default)");
    shell_error(shell, "  amplitude: 0-255 (default: 128)");
    shell_error(shell, "example: sa818 test tone 1000          (continuous 1 kHz tone)");
    shell_error(shell, "example: sa818 test tone 1000 5000     (1 kHz tone for 5 seconds)");
    shell_error(shell, "example: sa818 test tone 1750 3000 200 (1750 Hz for 3s at 78%% amplitude)");
    return -EINVAL;
  }

  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    shell_error(shell, "sa818 not ready");
    return -ENODEV;
  }

  /* Parse frequency */
  int freq_hz = atoi(argv[1]);
  if (freq_hz < 100 || freq_hz > 3000) {
    shell_error(shell, "invalid frequency: %d Hz (valid range: 100-3000 Hz)", freq_hz);
    return -EINVAL;
  }

  /* Parse optional duration */
  uint32_t duration_ms = 0;
  if (argc >= 3) {
    int duration = atoi(argv[2]);
    if (duration < 0) {
      shell_error(shell, "invalid duration: %d ms (must be non-negative)", duration);
      return -EINVAL;
    }
    duration_ms = static_cast<uint32_t>(duration);
  }

  /* Parse optional amplitude */
  uint8_t amplitude = 128; /* Default to 50% */
  if (argc >= 4) {
    int amp = atoi(argv[3]);
    if (amp < 0 || amp > 255) {
      shell_error(shell, "invalid amplitude: %d (valid range: 0-255)", amp);
      return -EINVAL;
    }
    amplitude = static_cast<uint8_t>(amp);
  }

  /* Generate test tone */
  sa818_result ret = sa818_audio_generate_test_tone(dev, static_cast<uint16_t>(freq_hz), duration_ms, amplitude);
  if (ret != SA818_OK) {
    shell_error(shell, "Failed to generate test tone: %d", ret);
    return ret;
  }

  if (duration_ms > 0) {
    shell_print(shell, "Test tone started: %d Hz for %u ms at amplitude %u", freq_hz, duration_ms, amplitude);
  } else {
    shell_print(shell, "Continuous test tone started: %d Hz at amplitude %u", freq_hz, amplitude);
  }

  return 0;
}

static int cmd_sa818_test_tone_stop(const struct shell *shell, size_t argc, char **argv) {
  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    shell_error(shell, "sa818 not ready");
    return -ENODEV;
  }

  sa818_result ret = sa818_audio_stop_test_tone(dev);
  if (ret != SA818_OK) {
    shell_error(shell, "Failed to stop test tone: %d", ret);
    return ret;
  }

  shell_print(shell, "Test tone stopped");
  return 0;
}

static int cmd_sa818_test_sweep(const struct shell *shell, size_t argc, char **argv) {
  if (argc < 3) {
    shell_error(shell, "usage: sa818 test sweep <start_hz> <end_hz> [duration_ms] [amplitude]");
    shell_error(shell, "  start_hz:    100-3000 Hz");
    shell_error(shell, "  end_hz:      100-3000 Hz (must be > start_hz)");
    shell_error(shell, "  duration_ms: 1000-60000 ms per sweep cycle (default: 10000)");
    shell_error(shell, "  amplitude:   0-255 (default: 128)");
    shell_error(shell, "  Note: the sweep loops continuously; stop it with 'sa818 test tone_stop'");
    return -EINVAL;
  }

  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    shell_error(shell, "sa818 not ready");
    return -ENODEV;
  }

  int start_freq = atoi(argv[1]);
  if (start_freq < 100 || start_freq > 3000) {
    shell_error(shell, "invalid start frequency: %d Hz (valid range: 100-3000 Hz)", start_freq);
    return -EINVAL;
  }

  int end_freq = atoi(argv[2]);
  if (end_freq < 100 || end_freq > 3000) {
    shell_error(shell, "invalid end frequency: %d Hz (valid range: 100-3000 Hz)", end_freq);
    return -EINVAL;
  }

  if (end_freq <= start_freq) {
    shell_error(shell, "end frequency must be greater than start frequency");
    return -EINVAL;
  }

  uint32_t duration_ms = 10000; /* default 10 s per cycle */
  if (argc >= 4) {
    int duration = atoi(argv[3]);
    if (duration < 1000 || duration > 60000) {
      shell_error(shell, "invalid duration: %d ms (valid range: 1000-60000 ms)", duration);
      return -EINVAL;
    }
    duration_ms = static_cast<uint32_t>(duration);
  }

  uint8_t amplitude = 128; /* default 50 %% */
  if (argc >= 5) {
    int amp = atoi(argv[4]);
    if (amp < 0 || amp > 255) {
      shell_error(shell, "invalid amplitude: %d (valid range: 0-255)", amp);
      return -EINVAL;
    }
    amplitude = static_cast<uint8_t>(amp);
  }

  sa818_result ret = sa818_audio_generate_sweep(dev, static_cast<uint16_t>(start_freq), static_cast<uint16_t>(end_freq), duration_ms, amplitude);
  if (ret != SA818_OK) {
    shell_error(shell, "Failed to start sweep: %d", ret);
    return ret;
  }

  shell_print(shell, "Frequency sweep started: %d Hz -> %d Hz, cycling every %u ms at amplitude %u", start_freq, end_freq, duration_ms, amplitude);
  shell_print(shell, "Use 'sa818 test tone_stop' to stop the sweep");
  return 0;
}

// clang-format off
SHELL_STATIC_SUBCMD_SET_CREATE(
    sa818_test_cmds,
    SHELL_CMD(tone, NULL, "Generate test tone", cmd_sa818_test_tone),
    SHELL_CMD(tone_stop, NULL, "Stop test tone", cmd_sa818_test_tone_stop),
    SHELL_CMD(sweep, NULL, "Continuous frequency sweep", cmd_sa818_test_sweep),
    SHELL_SUBCMD_SET_END);
SHELL_STATIC_SUBCMD_SET_CREATE(
    sa818_at_cmds,
    SHELL_CMD(connect, NULL, "Connection handshake", cmd_sa818_at_connect),
    SHELL_CMD(volume, NULL, "Set volume (1-8)", cmd_sa818_at_volume),
    SHELL_CMD(group, NULL, "Configure frequency", cmd_sa818_at_group),
    SHELL_CMD(filters, NULL, "Configure audio filters", cmd_sa818_at_filters),
    SHELL_CMD(rssi, NULL, "Read RSSI", cmd_sa818_at_rssi),
    SHELL_CMD(version, NULL, "Read firmware version", cmd_sa818_at_version),
    SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
    sa818_cmds,
    SHELL_CMD(status, NULL, "Show SA818 status", cmd_sa818_status),
    SHELL_CMD(power, NULL, "Power on/off", cmd_sa818_power),
    SHELL_CMD(ptt, NULL, "PTT on/off", cmd_sa818_ptt),
    SHELL_CMD(powerlevel, NULL, "Power level", cmd_sa818_powerlevel),
    SHELL_COND_CMD(CONFIG_GPIO_EMUL, sim_squelch, NULL, "Simulate squelch (sim only)", cmd_sa818_squelch_sim),
    SHELL_CMD(at, &sa818_at_cmds, "AT commands", NULL),
    SHELL_CMD(test, &sa818_test_cmds, "Test commands", NULL),
    SHELL_SUBCMD_SET_END);
// clang-format on

SHELL_CMD_REGISTER(sa818, &sa818_cmds, "SA818 control", NULL);

#endif /* CONFIG_SA818_SHELL */
