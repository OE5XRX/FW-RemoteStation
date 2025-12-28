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
  shell_print(shell, "powered=%d ptt=%d high_power=%d squelch=%d", st.device_power, st.ptt_state, st.power_level, st.squelch);
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
    sa818_set_power(dev, SA818_DEVICE_ON);
  } else if (!strcmp(argv[1], "off")) {
    sa818_set_power(dev, SA818_DEVICE_OFF);
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
    sa818_set_ptt(dev, SA818_PTT_ON);
  } else if (!strcmp(argv[1], "off")) {
    sa818_set_ptt(dev, SA818_PTT_OFF);
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
    sa818_set_power_level(dev, SA818_POWER_HIGH);
  } else if (!strcmp(argv[1], "low")) {
    sa818_set_power_level(dev, SA818_POWER_LOW);
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

// clang-format off
SHELL_STATIC_SUBCMD_SET_CREATE(
    sa818_cmds,
    SHELL_CMD(status, NULL, "Show SA818 status", cmd_sa818_status),
    SHELL_CMD(power, NULL, "Power on/off", cmd_sa818_power),
    SHELL_CMD(ptt, NULL, "PTT on/off", cmd_sa818_ptt),
    SHELL_CMD(powerlevel, NULL, "Power level", cmd_sa818_powerlevel),
    SHELL_COND_CMD(CONFIG_GPIO_EMUL, sim_squelch, NULL, "Simulate squelch (sim only)", cmd_sa818_squelch_sim),
    SHELL_SUBCMD_SET_END);
// clang-format on

SHELL_CMD_REGISTER(sa818, &sa818_cmds, "SA818 control", NULL);
