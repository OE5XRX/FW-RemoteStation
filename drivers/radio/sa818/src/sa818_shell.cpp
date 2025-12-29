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

#include <sa818/sa818.h>
#include <sa818/sa818_at.h>
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
 * Accepts: "narrow", "12.5", "wide", "25"
 */
static sa818_bandwidth parse_bandwidth(const char *str) {
  if (!strcmp(str, "narrow") || !strcmp(str, "12.5")) {
    return SA818_BW_12_5_KHZ;
  } else if (!strcmp(str, "wide") || !strcmp(str, "25")) {
    return SA818_BW_25_KHZ;
  }
  return static_cast<sa818_bandwidth>(atoi(str));
}

/**
 * @brief Parse CTCSS/DCS tone string to enum
 * Accepts: "none", "off", CTCSS frequency (e.g. "67.0"), or numeric code
 */
static sa818_tone_code parse_tone(const char *str) {
  if (!strcmp(str, "none") || !strcmp(str, "off")) {
    return SA818_TONE_NONE;
  }

  // Try to parse as CTCSS frequency (e.g. "67.0")
  float freq = atof(str);
  if (freq > 60.0f && freq < 260.0f) {
    // Lookup table for CTCSS frequency mapping
    static const struct {
      float min_freq;
      float max_freq;
      sa818_tone_code code;
    } ctcss_map[] = {
        {67.0f, 67.1f, SA818_CTCSS_67_0},
        {71.8f, 72.0f, SA818_CTCSS_71_9},
        {74.3f, 74.5f, SA818_CTCSS_74_4},
        {76.9f, 77.1f, SA818_CTCSS_77_0},
        {79.6f, 79.8f, SA818_CTCSS_79_7},
        {82.4f, 82.6f, SA818_CTCSS_82_5},
        {85.3f, 85.5f, SA818_CTCSS_85_4},
        {88.4f, 88.6f, SA818_CTCSS_88_5},
        {91.4f, 91.6f, SA818_CTCSS_91_5},
        {94.7f, 94.9f, SA818_CTCSS_94_8},
        {97.3f, 97.5f, SA818_CTCSS_97_4},
        {99.9f, 100.1f, SA818_CTCSS_100_0},
        {103.4f, 103.6f, SA818_CTCSS_103_5},
        {107.1f, 107.3f, SA818_CTCSS_107_2},
        {110.8f, 111.0f, SA818_CTCSS_110_9},
        {114.7f, 114.9f, SA818_CTCSS_114_8},
        {118.7f, 118.9f, SA818_CTCSS_118_8},
        {122.9f, 123.1f, SA818_CTCSS_123_0},
        {127.2f, 127.4f, SA818_CTCSS_127_3},
        {131.7f, 131.9f, SA818_CTCSS_131_8},
        {136.4f, 136.6f, SA818_CTCSS_136_5},
        {141.2f, 141.4f, SA818_CTCSS_141_3},
        {146.1f, 146.3f, SA818_CTCSS_146_2},
        {151.3f, 151.5f, SA818_CTCSS_151_4},
        {156.6f, 156.8f, SA818_CTCSS_156_7},
        {162.1f, 162.3f, SA818_CTCSS_162_2},
        {167.8f, 168.0f, SA818_CTCSS_167_9},
        {173.7f, 173.9f, SA818_CTCSS_173_8},
        {179.8f, 180.0f, SA818_CTCSS_179_9},
        {186.1f, 186.3f, SA818_CTCSS_186_2},
        {192.7f, 192.9f, SA818_CTCSS_192_8},
        {203.4f, 203.6f, SA818_CTCSS_203_5},
        {210.6f, 210.8f, SA818_CTCSS_210_7},
        {218.0f, 218.2f, SA818_CTCSS_218_1},
        {225.6f, 225.8f, SA818_CTCSS_225_7},
        {233.5f, 233.7f, SA818_CTCSS_233_6},
        {241.7f, 241.9f, SA818_CTCSS_241_8},
        {250.2f, 250.4f, SA818_CTCSS_250_3},
    };

    for (size_t i = 0; i < (sizeof(ctcss_map) / sizeof(ctcss_map[0])); ++i) {
      if (freq >= ctcss_map[i].min_freq && freq <= ctcss_map[i].max_freq) {
        return ctcss_map[i].code;
      }
    }
  }

  // Fall back to numeric code
  return static_cast<sa818_tone_code>(atoi(str));
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
  sa818_tone_code tx_tone = parse_tone(argv[4]);
  int squelch = atoi(argv[5]);
  sa818_tone_code rx_tone = parse_tone(argv[6]);

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

// clang-format off
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
    SHELL_SUBCMD_SET_END);
// clang-format on

SHELL_CMD_REGISTER(sa818, &sa818_cmds, "SA818 control", NULL);
