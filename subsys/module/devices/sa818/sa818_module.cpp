/**
 * @file sa818_module.cpp
 * @brief SA818 concrete implementation of the generic module capability framework.
 *
 * Defines the SA818-specific capabilities (frequency, ptt, power_level, rssi, volume,
 * bandwidth) as subclasses of the kind mixins in module_iface.h, wires them into a
 * @ref mod::Module registry, and registers the `module` Zephyr shell group. This is the
 * firmware half of the Firmware<->Agent contract (module-platform meta-spec §8); the
 * human `sa818` command tree stays separate and untouched.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#ifdef CONFIG_MODULE_SA818

#include <math.h>
#include <oe5xrx/module/iface.h>
#include <optional>
#include <sa818/sa818.h>
#include <sa818/sa818_at.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

namespace {

using mod::Action;
using mod::Capability;
using mod::FieldSpec;
using mod::Identity;
using mod::Module;
using mod::Op;
using mod::Result;
using mod::Setting;
using mod::Telemetry;
using mod::ValueType;

/**
 * @brief Shared SA818 context: the device handle plus the RAM group shadow.
 *
 * The shadow lets `set frequency` / `set bandwidth` rebuild the full sa818_at_set_group()
 * call (the driver has no frequency-only entry point). Working state only -- NOT capability
 * persistence. Seeded to the module's power-on defaults (matching the SA818Simulator).
 */
struct Sa818Context {
  const struct device *dev;
  sa818_bandwidth bw;
  float freq;
  sa818_tone_code tone;
  sa818_squelch_level squelch;

  bool ready() const { return dev != nullptr && device_is_ready(dev); }
};

/* Value parsers: return the parsed value, or nullopt on malformed input. Callers apply
 * any capability-specific range/enum check on the parsed value. (Exceptions are disabled,
 * so callers dereference with * / value_or after checking, never with .value().) */

std::optional<bool> parse_bool(const char *s) {
  if (!strcmp(s, "on") || !strcmp(s, "1") || !strcmp(s, "true")) {
    return true;
  }
  if (!strcmp(s, "off") || !strcmp(s, "0") || !strcmp(s, "false")) {
    return false;
  }
  return std::nullopt;
}

std::optional<float> parse_float(const char *s) {
  char *end = nullptr;
  float f = strtof(s, &end);
  if (end == s || *end != '\0' || !isfinite(f)) {
    return std::nullopt;
  }
  return f;
}

std::optional<long> parse_int(const char *s) {
  char *end = nullptr;
  long v = strtol(s, &end, 10);
  if (end == s || *end != '\0') {
    return std::nullopt;
  }
  return v;
}

/* SA818-V capability constraints (single source of truth for both the descriptor and
 * the runtime validation). */
constexpr float FREQ_MIN_MHZ = 144.0f;
constexpr float FREQ_MAX_MHZ = 148.0f;
constexpr int VOLUME_MIN = 1;
constexpr int VOLUME_MAX = 8;

/* Output buffer sizes (bounded by CONFIG_SHELL_CMD_BUFF_SIZE on the input side). */
constexpr size_t RESULT_BUF_SIZE = 768;
constexpr size_t DESCRIBE_BUF_SIZE = 1024;

/* Enum value strings: defined once, used for BOTH the descriptor tables below and the
 * parse/serialize logic in the capabilities, so the advertised enum and the accepted
 * input can never drift apart. */
constexpr const char *POWER_LOW = "low";
constexpr const char *POWER_HIGH = "high";
constexpr const char *BW_NARROW = "12.5";
constexpr const char *BW_WIDE = "25";

const char *const POWER_LEVELS[] = {POWER_LOW, POWER_HIGH};
const char *const BANDWIDTHS[] = {BW_NARROW, BW_WIDE};

const FieldSpec FREQ_SPEC{"frequency", ValueType::Float, "MHz", true, FREQ_MIN_MHZ, FREQ_MAX_MHZ};
const FieldSpec PTT_SPEC{"ptt", ValueType::Bool};
const FieldSpec POWER_SPEC{"power_level", ValueType::Enum, nullptr, false, 0.0, 0.0, POWER_LEVELS, 2};
const FieldSpec RSSI_SPEC{"rssi", ValueType::Int, "dBm", false, 0.0, 0.0, nullptr, 0, /*readonly=*/true};
const FieldSpec VOLUME_SPEC{"volume", ValueType::Int, nullptr, true, VOLUME_MIN, VOLUME_MAX};
const FieldSpec BW_SPEC{"bandwidth", ValueType::Enum, "kHz", false, 0.0, 0.0, BANDWIDTHS, 2};

class FrequencyCap : public Setting {
public:
  explicit FrequencyCap(Sa818Context &ctx) : ctx_(ctx) {}
  const FieldSpec &spec() const override { return FREQ_SPEC; }

protected:
  Result onSet(const char *value) override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    std::optional<float> f = parse_float(value);
    if (!f) {
      return Result::err("bad_value");
    }
    if (*f < FREQ_MIN_MHZ || *f > FREQ_MAX_MHZ) {
      return Result::err("out_of_range");
    }
    if (sa818_at_set_group(ctx_.dev, ctx_.bw, *f, *f, ctx_.tone, ctx_.squelch, ctx_.tone) != SA818_OK) {
      return Result::err("driver_error");
    }
    ctx_.freq = *f; // commit shadow only after the driver call succeeds
    return Result::okFloat(static_cast<double>(*f));
  }

  Result onGet() override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    return Result::okFloat(static_cast<double>(ctx_.freq));
  }

private:
  Sa818Context &ctx_;
};

class BandwidthCap : public Setting {
public:
  explicit BandwidthCap(Sa818Context &ctx) : ctx_(ctx) {}
  const FieldSpec &spec() const override { return BW_SPEC; }

protected:
  Result onSet(const char *value) override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    sa818_bandwidth bw;
    if (!strcmp(value, BW_NARROW)) {
      bw = SA818_BW_12_5_KHZ;
    } else if (!strcmp(value, BW_WIDE)) {
      bw = SA818_BW_25_KHZ;
    } else {
      return Result::err("bad_value");
    }
    if (sa818_at_set_group(ctx_.dev, bw, ctx_.freq, ctx_.freq, ctx_.tone, ctx_.squelch, ctx_.tone) != SA818_OK) {
      return Result::err("driver_error");
    }
    ctx_.bw = bw; // commit shadow only after the driver call succeeds
    return Result::okStr(value);
  }

  Result onGet() override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    return Result::okStr(ctx_.bw == SA818_BW_25_KHZ ? BW_WIDE : BW_NARROW);
  }

private:
  Sa818Context &ctx_;
};

class PowerLevelCap : public Setting {
public:
  explicit PowerLevelCap(Sa818Context &ctx) : ctx_(ctx) {}
  const FieldSpec &spec() const override { return POWER_SPEC; }

protected:
  Result onSet(const char *value) override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    sa818_power_level level;
    if (!strcmp(value, POWER_HIGH)) {
      level = SA818_POWER_HIGH;
    } else if (!strcmp(value, POWER_LOW)) {
      level = SA818_POWER_LOW;
    } else {
      return Result::err("bad_value");
    }
    if (sa818_set_power_level(ctx_.dev, level) != SA818_OK) {
      return Result::err("driver_error");
    }
    return Result::okStr(value);
  }

  Result onGet() override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    sa818_status st = sa818_get_status(ctx_.dev);
    return Result::okStr(st.power_level == SA818_POWER_HIGH ? POWER_HIGH : POWER_LOW);
  }

private:
  Sa818Context &ctx_;
};

class VolumeCap : public Setting {
public:
  explicit VolumeCap(Sa818Context &ctx) : ctx_(ctx) {}
  const FieldSpec &spec() const override { return VOLUME_SPEC; }

protected:
  Result onSet(const char *value) override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    std::optional<long> v = parse_int(value);
    if (!v) {
      return Result::err("bad_value");
    }
    if (*v < VOLUME_MIN || *v > VOLUME_MAX) {
      return Result::err("out_of_range");
    }
    if (sa818_at_set_volume(ctx_.dev, static_cast<sa818_volume_level>(*v)) != SA818_OK) {
      return Result::err("driver_error");
    }
    return Result::okInt(static_cast<int>(*v));
  }

  Result onGet() override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    sa818_status st = sa818_get_status(ctx_.dev);
    return Result::okInt(static_cast<int>(st.volume));
  }

private:
  Sa818Context &ctx_;
};

class PttCap : public Action {
public:
  explicit PttCap(Sa818Context &ctx) : ctx_(ctx) {}
  const FieldSpec &spec() const override { return PTT_SPEC; }

protected:
  Result onDo(const char *value) override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    std::optional<bool> on = parse_bool(value);
    if (!on) {
      return Result::err("bad_value");
    }
    if (sa818_set_ptt(ctx_.dev, *on ? SA818_PTT_ON : SA818_PTT_OFF) != SA818_OK) {
      return Result::err("driver_error");
    }
    return Result::okBool(*on);
  }

  Result onGet() override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    sa818_status st = sa818_get_status(ctx_.dev);
    return Result::okBool(st.ptt_state == SA818_PTT_ON);
  }

private:
  Sa818Context &ctx_;
};

class RssiCap : public Telemetry {
public:
  explicit RssiCap(Sa818Context &ctx) : ctx_(ctx) {}
  const FieldSpec &spec() const override { return RSSI_SPEC; }

protected:
  Result onGet() override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    uint8_t rssi = 0;
    if (sa818_at_read_rssi(ctx_.dev, &rssi) != SA818_OK) {
      return Result::err("driver_error");
    }
    return Result::okInt(static_cast<int>(rssi));
  }

private:
  Sa818Context &ctx_;
};

/* Registry: one shared context + one instance per capability, all statically allocated. */
Sa818Context g_ctx{DEVICE_DT_GET_OR_NULL(DT_NODELABEL(sa818)), SA818_BW_12_5_KHZ, 145.500f, SA818_TONE_NONE, SA818_SQL_LEVEL_4};

FrequencyCap g_freq{g_ctx};
PttCap g_ptt{g_ctx};
PowerLevelCap g_power{g_ctx};
RssiCap g_rssi{g_ctx};
VolumeCap g_volume{g_ctx};
BandwidthCap g_bandwidth{g_ctx};

Capability *const g_caps[] = {&g_freq, &g_ptt, &g_power, &g_rssi, &g_volume, &g_bandwidth};
const Identity g_identity{"fm_transceiver", "SA818-V", "2m"};
Module g_module{g_identity, g_caps};

void emit_result(const struct shell *sh, const Result &r, const char *cap, Op op) {
  char buf[RESULT_BUF_SIZE];
  mod::JsonWriter w(buf, sizeof(buf));
  w.raw("MODULE-RESULT ");
  r.render(w, cap, mod::opStr(op));
  if (w.truncated()) {
    // Pathologically long cap/value: fall back to a guaranteed-valid short result
    // rather than emit truncated (invalid) JSON. op is a short internal literal.
    shell_print(sh, "MODULE-RESULT {\"ok\":false,\"cap\":\"\",\"op\":\"%s\",\"error\":\"too_long\"}", mod::opStr(op));
    return;
  }
  shell_print(sh, "%s", w.c_str());
}

int cmd_module_describe(const struct shell *sh, size_t, char **) {
  char buf[DESCRIBE_BUF_SIZE];
  mod::JsonWriter w(buf, sizeof(buf));
  w.raw("MODULE-DESCRIBE ");
  g_module.describe(w);
  if (w.truncated()) {
    // Descriptor outgrew the buffer: emit a minimal valid descriptor rather than
    // truncated (invalid) JSON, so the machine-readable contract still holds.
    shell_print(sh, "MODULE-DESCRIBE {\"schema\":1,\"error\":\"too_long\"}");
    return 0;
  }
  shell_print(sh, "%s", w.c_str());
  return 0;
}

int cmd_module_set(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 3) {
    emit_result(sh, Result::err("usage"), argc >= 2 ? argv[1] : "", Op::Set);
    return 0;
  }
  emit_result(sh, g_module.execute(Op::Set, argv[1], argv[2]), argv[1], Op::Set);
  return 0;
}

int cmd_module_get(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 2) {
    emit_result(sh, Result::err("usage"), "", Op::Get);
    return 0;
  }
  emit_result(sh, g_module.execute(Op::Get, argv[1], ""), argv[1], Op::Get);
  return 0;
}

int cmd_module_do(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 3) {
    emit_result(sh, Result::err("usage"), argc >= 2 ? argv[1] : "", Op::Do);
    return 0;
  }
  emit_result(sh, g_module.execute(Op::Do, argv[1], argv[2]), argv[1], Op::Do);
  return 0;
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

#endif /* CONFIG_MODULE_SA818 */
