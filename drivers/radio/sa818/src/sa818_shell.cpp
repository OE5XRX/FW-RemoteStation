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
static int cmd_sa818_at_volume(const struct shell *shell, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(shell, "usage: sa818 at_volume <1-8>");
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

  sa818_result ret = sa818_at_set_volume(dev, volume);
  if (ret != SA818_OK) {
    shell_error(shell, "AT command failed: %d", ret);
    return ret;
  }

  shell_print(shell, "Volume set to %d", volume);
  return 0;
}

static int cmd_sa818_at_group(const struct shell *shell, size_t argc, char **argv) {
  if (argc < 7) {
    shell_error(shell, "usage: sa818 at_group <bw> <tx_freq> <rx_freq> <tx_ctcss> <squelch> <rx_ctcss>");
    shell_error(shell, "example: sa818 at_group 0 145.500 145.500 0 4 0");
    return -EINVAL;
  }

  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    shell_error(shell, "sa818 not ready");
    return -ENODEV;
  }

  int bw = atoi(argv[1]);
  float tx_freq = atof(argv[2]);
  float rx_freq = atof(argv[3]);
  int tx_ctcss = atoi(argv[4]);
  int squelch = atoi(argv[5]);
  int rx_ctcss = atoi(argv[6]);

  sa818_result ret = sa818_at_set_group(dev, bw, tx_freq, rx_freq, tx_ctcss, squelch, rx_ctcss);
  if (ret != SA818_OK) {
    shell_error(shell, "AT command failed: %d", ret);
    return ret;
  }

  shell_print(shell, "Group configured: TX=%.3f RX=%.3f SQ=%d", (double)tx_freq, (double)rx_freq, squelch);
  return 0;
}

static int cmd_sa818_at_filters(const struct shell *shell, size_t argc, char **argv) {
  if (argc < 4) {
    shell_error(shell, "usage: sa818 at_filters <pre> <hpf> <lpf>");
    shell_error(shell, "example: sa818 at_filters 1 1 1");
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

  sa818_result ret = sa818_at_set_filters(dev, pre, hpf, lpf);
  if (ret != SA818_OK) {
    shell_error(shell, "AT command failed: %d", ret);
    return ret;
  }

  shell_print(shell, "Filters: PRE=%d HPF=%d LPF=%d", pre, hpf, lpf);
  return 0;
}

// clang-format off
SHELL_STATIC_SUBCMD_SET_CREATE(
    sa818_cmds,
    SHELL_CMD(status, NULL, "Show SA818 status", cmd_sa818_status),
    SHELL_CMD(power, NULL, "Power on/off", cmd_sa818_power),
    SHELL_CMD(ptt, NULL, "PTT on/off", cmd_sa818_ptt),
    SHELL_CMD(powerlevel, NULL, "Power level", cmd_sa818_powerlevel),
    SHELL_COND_CMD(CONFIG_GPIO_EMUL, sim_squelch, NULL, "Simulate squelch (sim only)", cmd_sa818_squelch_sim),
    SHELL_CMD(at_volume, NULL, "Set volume via AT (1-8)", cmd_sa818_at_volume),
    SHELL_CMD(at_group, NULL, "Configure frequency via AT", cmd_sa818_at_group),
    SHELL_CMD(at_filters, NULL, "Configure audio filters via AT", cmd_sa818_at_filters),
    SHELL_SUBCMD_SET_END);
// clang-format on

SHELL_CMD_REGISTER(sa818, &sa818_cmds, "SA818 control", NULL);
