/**
 * @file sa818_module.cpp
 * @brief Generic machine-readable module interface for the SA818.
 *
 * Registers a `module` shell command group (describe / set / get / do) that maps
 * the generic {capability, op, value} contract onto the SA818 driver. This is the
 * firmware half of the Firmware<->Agent contract (module-platform meta-spec §8).
 * The human `sa818` command tree stays separate and untouched.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#ifdef CONFIG_SA818_MODULE_IFACE

#include <sa818/sa818.h>
#include <sa818/sa818_at.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

namespace {

[[maybe_unused]] const struct device *sa818_dev(void) {
  return DEVICE_DT_GET_OR_NULL(DT_NODELABEL(sa818));
}

/* RAM shadow of the group config so `set frequency` / `set bandwidth` can rebuild the
 * full sa818_at_set_group() call. Seeded to the module's power-on defaults (matching the
 * SA818Simulator defaults). Working state only -- NOT capability persistence. */
struct group_shadow {
  sa818_bandwidth bw;
  float freq;
  sa818_tone_code tone;
  sa818_squelch_level squelch;
};
[[maybe_unused]] group_shadow g_shadow = {SA818_BW_12_5_KHZ, 145.500f, SA818_TONE_NONE, SA818_SQL_LEVEL_4};

/* Descriptor fragments -- single source of truth for `module describe`. */
struct capability {
  const char *name;
  const char *json;
};
const capability CAPS[] = {
    {"frequency",
     "{\"name\":\"frequency\",\"kind\":\"setting\",\"type\":\"float\",\"unit\":\"MHz\",\"min\":144.0,\"max\":148.0,\"step\":0.0125,\"access\":\"operator\"}"},
    {"ptt", "{\"name\":\"ptt\",\"kind\":\"action\",\"type\":\"bool\",\"access\":\"operator\"}"},
    {"power_level", "{\"name\":\"power_level\",\"kind\":\"setting\",\"type\":\"enum\",\"values\":[\"low\",\"high\"],\"access\":\"operator\"}"},
    {"rssi", "{\"name\":\"rssi\",\"kind\":\"telemetry\",\"type\":\"int\",\"unit\":\"dBm\",\"readonly\":true,\"access\":\"operator\"}"},
    {"volume", "{\"name\":\"volume\",\"kind\":\"setting\",\"type\":\"int\",\"min\":1,\"max\":8,\"access\":\"operator\"}"},
    {"bandwidth", "{\"name\":\"bandwidth\",\"kind\":\"setting\",\"type\":\"enum\",\"values\":[\"12.5\",\"25\"],\"unit\":\"kHz\",\"access\":\"operator\"}"},
};

void result_err(const struct shell *sh, const char *cap, const char *op, const char *err) {
  shell_print(sh, "MODULE-RESULT {\"ok\":false,\"cap\":\"%s\",\"op\":\"%s\",\"error\":\"%s\"}", cap, op, err);
}

[[maybe_unused]] void result_ok_int(const struct shell *sh, const char *cap, const char *op, int value) {
  shell_print(sh, "MODULE-RESULT {\"ok\":true,\"cap\":\"%s\",\"op\":\"%s\",\"value\":%d}", cap, op, value);
}

[[maybe_unused]] void result_ok_float(const struct shell *sh, const char *cap, const char *op, double value) {
  shell_print(sh, "MODULE-RESULT {\"ok\":true,\"cap\":\"%s\",\"op\":\"%s\",\"value\":%g}", cap, op, value);
}

[[maybe_unused]] void result_ok_bool(const struct shell *sh, const char *cap, const char *op, bool value) {
  shell_print(sh, "MODULE-RESULT {\"ok\":true,\"cap\":\"%s\",\"op\":\"%s\",\"value\":%s}", cap, op, value ? "true" : "false");
}

[[maybe_unused]] void result_ok_str(const struct shell *sh, const char *cap, const char *op, const char *value) {
  shell_print(sh, "MODULE-RESULT {\"ok\":true,\"cap\":\"%s\",\"op\":\"%s\",\"value\":\"%s\"}", cap, op, value);
}

void emit_describe(const struct shell *sh) {
  static char buf[1024];
  int n = snprintf(buf, sizeof(buf),
                   "MODULE-DESCRIBE {\"schema\":1,\"identity\":{\"type\":\"fm_transceiver\",\"model\":\"SA818-V\",\"version\":"
                   "\"2m\"},\"capabilities\":[");
  for (size_t i = 0; i < ARRAY_SIZE(CAPS); ++i) {
    n += snprintf(buf + n, sizeof(buf) - n, "%s%s", i ? "," : "", CAPS[i].json);
  }
  snprintf(buf + n, sizeof(buf) - n, "]}");
  shell_print(sh, "%s", buf);
}

/* Dispatchers -- fleshed out in later tasks. */
int do_set(const struct shell *sh, const char *cap, const char *valstr);
int do_get(const struct shell *sh, const char *cap);
int do_do(const struct shell *sh, const char *cap, const char *valstr);

int do_set(const struct shell *sh, const char *cap, const char *) {
  result_err(sh, cap, "set", "unknown_capability");
  return 0;
}
int do_get(const struct shell *sh, const char *cap) {
  result_err(sh, cap, "get", "unknown_capability");
  return 0;
}
int do_do(const struct shell *sh, const char *cap, const char *) {
  result_err(sh, cap, "do", "unknown_capability");
  return 0;
}

int cmd_module_describe(const struct shell *sh, size_t, char **) {
  emit_describe(sh);
  return 0;
}

int cmd_module_set(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 3) {
    result_err(sh, argc >= 2 ? argv[1] : "", "set", "usage");
    return 0;
  }
  return do_set(sh, argv[1], argv[2]);
}

int cmd_module_get(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 2) {
    result_err(sh, "", "get", "usage");
    return 0;
  }
  return do_get(sh, argv[1]);
}

int cmd_module_do(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 3) {
    result_err(sh, argc >= 2 ? argv[1] : "", "do", "usage");
    return 0;
  }
  return do_do(sh, argv[1], argv[2]);
}

} // namespace

// clang-format off
SHELL_STATIC_SUBCMD_SET_CREATE(
    module_cmds,
    SHELL_CMD_ARG(describe, NULL, "Emit machine-readable module descriptor", cmd_module_describe, 1, 0),
    SHELL_CMD_ARG(set, NULL, "set <capability> <value>", cmd_module_set, 3, 0),
    SHELL_CMD_ARG(get, NULL, "get <capability>", cmd_module_get, 2, 0),
    SHELL_CMD_ARG(do, NULL, "do <capability> <value>", cmd_module_do, 3, 0),
    SHELL_SUBCMD_SET_END);
// clang-format on

SHELL_CMD_REGISTER(module, &module_cmds, "Generic module interface (describe/set/get/do)", NULL);

#endif /* CONFIG_SA818_MODULE_IFACE */
