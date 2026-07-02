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

#include <math.h>
#include <sa818/sa818.h>
#include <sa818/sa818_at.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

namespace {

const struct device *sa818_dev(void) {
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
group_shadow g_shadow = {SA818_BW_12_5_KHZ, 145.500f, SA818_TONE_NONE, SA818_SQL_LEVEL_4};

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

void result_ok_int(const struct shell *sh, const char *cap, const char *op, int value) {
  shell_print(sh, "MODULE-RESULT {\"ok\":true,\"cap\":\"%s\",\"op\":\"%s\",\"value\":%d}", cap, op, value);
}

void result_ok_float(const struct shell *sh, const char *cap, const char *op, double value) {
  shell_print(sh, "MODULE-RESULT {\"ok\":true,\"cap\":\"%s\",\"op\":\"%s\",\"value\":%.4f}", cap, op, value);
}

void result_ok_bool(const struct shell *sh, const char *cap, const char *op, bool value) {
  shell_print(sh, "MODULE-RESULT {\"ok\":true,\"cap\":\"%s\",\"op\":\"%s\",\"value\":%s}", cap, op, value ? "true" : "false");
}

void result_ok_str(const struct shell *sh, const char *cap, const char *op, const char *value) {
  shell_print(sh, "MODULE-RESULT {\"ok\":true,\"cap\":\"%s\",\"op\":\"%s\",\"value\":\"%s\"}", cap, op, value);
}

void emit_describe(const struct shell *sh) {
  static char buf[1024];
  int n = snprintf(buf, sizeof(buf),
                   "MODULE-DESCRIBE {\"schema\":1,\"identity\":{\"type\":\"fm_transceiver\","
                   "\"model\":\"SA818-V\",\"version\":\"2m\"},\"capabilities\":[");
  for (size_t i = 0; i < ARRAY_SIZE(CAPS); ++i) {
    if (n < 0 || (size_t)n >= sizeof(buf)) {
      break; // truncation guard
    }
    n += snprintf(buf + n, sizeof(buf) - n, "%s%s", i ? "," : "", CAPS[i].json);
  }
  if (n >= 0 && (size_t)n < sizeof(buf)) {
    snprintf(buf + n, sizeof(buf) - n, "]}");
  }
  shell_print(sh, "%s", buf);
}

/* Parse on/off/1/0/true/false. Returns true on success and writes *out. */
bool parse_bool(const char *s, bool *out) {
  if (!strcmp(s, "on") || !strcmp(s, "1") || !strcmp(s, "true")) {
    *out = true;
    return true;
  }
  if (!strcmp(s, "off") || !strcmp(s, "0") || !strcmp(s, "false")) {
    *out = false;
    return true;
  }
  return false;
}

enum cap_kind { KIND_UNKNOWN, KIND_SETTING, KIND_ACTION, KIND_TELEMETRY };

cap_kind kind_of(const char *cap) {
  if (!strcmp(cap, "frequency") || !strcmp(cap, "power_level") || !strcmp(cap, "volume") || !strcmp(cap, "bandwidth")) {
    return KIND_SETTING;
  }
  if (!strcmp(cap, "ptt")) {
    return KIND_ACTION;
  }
  if (!strcmp(cap, "rssi")) {
    return KIND_TELEMETRY;
  }
  return KIND_UNKNOWN;
}

/* Dispatchers -- fleshed out in later tasks. */
int do_set(const struct shell *sh, const char *cap, const char *valstr) {
  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    result_err(sh, cap, "set", "driver_error");
    return 0;
  }

  switch (kind_of(cap)) {
  case KIND_UNKNOWN:
    result_err(sh, cap, "set", "unknown_capability");
    return 0;
  case KIND_TELEMETRY:
    result_err(sh, cap, "set", "read_only");
    return 0;
  case KIND_ACTION:
    result_err(sh, cap, "set", "wrong_op");
    return 0;
  case KIND_SETTING:
    break;
  }

  if (!strcmp(cap, "frequency")) {
    char *end = nullptr;
    float f = strtof(valstr, &end);
    if (end == valstr || *end != '\0') {
      result_err(sh, cap, "set", "bad_value");
      return 0;
    }
    if (!isfinite(f)) {
      result_err(sh, cap, "set", "bad_value");
      return 0;
    }
    if (f < 144.0f || f > 148.0f) {
      result_err(sh, cap, "set", "out_of_range");
      return 0;
    }
    sa818_result r = sa818_at_set_group(dev, g_shadow.bw, f, f, g_shadow.tone, g_shadow.squelch, g_shadow.tone);
    if (r != SA818_OK) {
      result_err(sh, cap, "set", "driver_error");
      return 0;
    }
    g_shadow.freq = f; // commit shadow only after the driver call succeeds
    result_ok_float(sh, cap, "set", (double)f);
    return 0;
  }

  if (!strcmp(cap, "power_level")) {
    if (!strcmp(valstr, "high")) {
      if (sa818_set_power_level(dev, SA818_POWER_HIGH) != SA818_OK) {
        result_err(sh, cap, "set", "driver_error");
        return 0;
      }
    } else if (!strcmp(valstr, "low")) {
      if (sa818_set_power_level(dev, SA818_POWER_LOW) != SA818_OK) {
        result_err(sh, cap, "set", "driver_error");
        return 0;
      }
    } else {
      result_err(sh, cap, "set", "bad_value");
      return 0;
    }
    result_ok_str(sh, cap, "set", valstr);
    return 0;
  }

  if (!strcmp(cap, "volume")) {
    char *end = nullptr;
    long v = strtol(valstr, &end, 10);
    if (end == valstr || *end != '\0') {
      result_err(sh, cap, "set", "bad_value");
      return 0;
    }
    if (v < 1 || v > 8) {
      result_err(sh, cap, "set", "out_of_range");
      return 0;
    }
    if (sa818_at_set_volume(dev, static_cast<sa818_volume_level>(v)) != SA818_OK) {
      result_err(sh, cap, "set", "driver_error");
      return 0;
    }
    result_ok_int(sh, cap, "set", (int)v);
    return 0;
  }

  if (!strcmp(cap, "bandwidth")) {
    sa818_bandwidth bw;
    if (!strcmp(valstr, "12.5")) {
      bw = SA818_BW_12_5_KHZ;
    } else if (!strcmp(valstr, "25")) {
      bw = SA818_BW_25_KHZ;
    } else {
      result_err(sh, cap, "set", "bad_value");
      return 0;
    }
    if (sa818_at_set_group(dev, bw, g_shadow.freq, g_shadow.freq, g_shadow.tone, g_shadow.squelch, g_shadow.tone) != SA818_OK) {
      result_err(sh, cap, "set", "driver_error");
      return 0;
    }
    g_shadow.bw = bw; // commit shadow only after the driver call succeeds
    result_ok_str(sh, cap, "set", valstr);
    return 0;
  }

  result_err(sh, cap, "set", "unknown_capability");
  return 0;
}

int do_get(const struct shell *sh, const char *cap) {
  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    result_err(sh, cap, "get", "driver_error");
    return 0;
  }

  if (!strcmp(cap, "rssi")) {
    uint8_t rssi = 0;
    if (sa818_at_read_rssi(dev, &rssi) != SA818_OK) {
      result_err(sh, cap, "get", "driver_error");
      return 0;
    }
    result_ok_int(sh, cap, "get", (int)rssi);
    return 0;
  }
  if (!strcmp(cap, "frequency")) {
    result_ok_float(sh, cap, "get", (double)g_shadow.freq);
    return 0;
  }
  if (!strcmp(cap, "bandwidth")) {
    result_ok_str(sh, cap, "get", g_shadow.bw == SA818_BW_25_KHZ ? "25" : "12.5");
    return 0;
  }

  sa818_status st = sa818_get_status(dev);
  if (!strcmp(cap, "volume")) {
    result_ok_int(sh, cap, "get", (int)st.volume);
    return 0;
  }
  if (!strcmp(cap, "power_level")) {
    result_ok_str(sh, cap, "get", st.power_level == SA818_POWER_HIGH ? "high" : "low");
    return 0;
  }
  if (!strcmp(cap, "ptt")) {
    result_ok_bool(sh, cap, "get", st.ptt_state == SA818_PTT_ON);
    return 0;
  }

  result_err(sh, cap, "get", "unknown_capability");
  return 0;
}
int do_do(const struct shell *sh, const char *cap, const char *valstr) {
  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    result_err(sh, cap, "do", "driver_error");
    return 0;
  }

  cap_kind kind = kind_of(cap);
  if (kind == KIND_UNKNOWN) {
    result_err(sh, cap, "do", "unknown_capability");
    return 0;
  }
  if (kind != KIND_ACTION) {
    result_err(sh, cap, "do", "wrong_op");
    return 0;
  }

  if (!strcmp(cap, "ptt")) {
    bool on;
    if (!parse_bool(valstr, &on)) {
      result_err(sh, cap, "do", "bad_value");
      return 0;
    }
    if (sa818_set_ptt(dev, on ? SA818_PTT_ON : SA818_PTT_OFF) != SA818_OK) {
      result_err(sh, cap, "do", "driver_error");
      return 0;
    }
    result_ok_bool(sh, cap, "do", on);
    return 0;
  }

  /* Should not be reached: kind_of guards above handle unknown/non-action. */
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
