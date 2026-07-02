/**
 * @file sa818_module.cpp
 * @brief SA818 concrete implementation of the generic module capability framework.
 *
 * Defines the SA818-specific capabilities (frequency, ptt, power_level, rssi, volume,
 * bandwidth) as subclasses of the kind mixins in oe5xrx/module/iface.h, wires them into a
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
using mod::ModuleRegistry;
using mod::Op;
using mod::Range;
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
  float freq_tx;
  float freq_rx;
  sa818_tone_code tone_tx;
  sa818_tone_code tone_rx;
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

/* Band selected from devicetree (sa818 node `band` property). */
#define SA818_BAND_IDX DT_ENUM_IDX(DT_NODELABEL(sa818), band) // 0 = vhf, 1 = uhf

#if SA818_BAND_IDX == 1
constexpr const char *BAND_NAME = "uhf";
constexpr const char *BAND_MODEL = "SA818-U";
const Range FREQ_RANGES[] = {{"uhf", 400.0, 480.0}};
#else
constexpr const char *BAND_NAME = "vhf";
constexpr const char *BAND_MODEL = "SA818-V";
const Range FREQ_RANGES[] = {{"vhf", 134.0, 174.0}};
#endif

/* SA818 capability constraints (single source of truth for both the descriptor and
 * the runtime validation). */
const Range VOLUME_RANGES[] = {{nullptr, 1.0, 8.0}};
const Range SQUELCH_RANGES[] = {{nullptr, 0.0, 8.0}};

/* Output buffer sizes (bounded by CONFIG_SHELL_CMD_BUFF_SIZE on the input side). */
constexpr size_t RESULT_BUF_SIZE = 768;
constexpr size_t DESCRIBE_BUF_SIZE = 2048;

/* Enum value strings: defined once, used for BOTH the descriptor tables below and the
 * parse/serialize logic in the capabilities, so the advertised enum and the accepted
 * input can never drift apart. */
constexpr const char *POWER_LOW = "low";
constexpr const char *POWER_HIGH = "high";
constexpr const char *BW_NARROW = "12.5";
constexpr const char *BW_WIDE = "25";

const char *const POWER_LEVELS[] = {POWER_LOW, POWER_HIGH};
const char *const BANDWIDTHS[] = {BW_NARROW, BW_WIDE};

const FieldSpec FREQ_SPEC{"frequency", ValueType::Float, "MHz", FREQ_RANGES, 1};
const FieldSpec TXFREQ_SPEC{"tx_frequency", ValueType::Float, "MHz", FREQ_RANGES, 1};
const FieldSpec RXFREQ_SPEC{"rx_frequency", ValueType::Float, "MHz", FREQ_RANGES, 1};
const FieldSpec PTT_SPEC{"ptt", ValueType::Bool};
const FieldSpec POWER_SPEC{"power_level", ValueType::Enum, nullptr, nullptr, 0, POWER_LEVELS, 2};
const FieldSpec RSSI_SPEC{"rssi", ValueType::Int, "raw", nullptr, 0, nullptr, 0, /*readonly=*/true};
const FieldSpec VOLUME_SPEC{"volume", ValueType::Int, nullptr, VOLUME_RANGES, 1};
const FieldSpec BW_SPEC{"bandwidth", ValueType::Enum, "kHz", nullptr, 0, BANDWIDTHS, 2};
const FieldSpec SQUELCH_SPEC{"squelch", ValueType::Int, nullptr, SQUELCH_RANGES, 1};
const FieldSpec TXTONE_SPEC{"tx_tone", ValueType::String};
const FieldSpec RXTONE_SPEC{"rx_tone", ValueType::String};
const FieldSpec BAND_SPEC{"band", ValueType::String, nullptr, nullptr, 0, nullptr, 0, /*readonly=*/true};

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
    if (!FREQ_SPEC.inAnyRange(static_cast<double>(*f))) {
      return Result::err("out_of_range");
    }
    if (sa818_at_set_group(ctx_.dev, ctx_.bw, *f, *f, ctx_.tone_tx, ctx_.squelch, ctx_.tone_rx) != SA818_OK) {
      return Result::err("driver_error");
    }
    ctx_.freq_tx = *f; // commit shadow only after the driver call succeeds
    ctx_.freq_rx = *f;
    return Result::okFloat(static_cast<double>(*f));
  }

  Result onGet() override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    return Result::okFloat(static_cast<double>(ctx_.freq_rx));
  }

private:
  Sa818Context &ctx_;
};

class TxFrequencyCap : public Setting {
public:
  explicit TxFrequencyCap(Sa818Context &ctx) : ctx_(ctx) {}
  const FieldSpec &spec() const override { return TXFREQ_SPEC; }

protected:
  Result onSet(const char *value) override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    std::optional<float> f = parse_float(value);
    if (!f) {
      return Result::err("bad_value");
    }
    if (!TXFREQ_SPEC.inAnyRange(static_cast<double>(*f))) {
      return Result::err("out_of_range");
    }
    if (sa818_at_set_group(ctx_.dev, ctx_.bw, *f, ctx_.freq_rx, ctx_.tone_tx, ctx_.squelch, ctx_.tone_rx) != SA818_OK) {
      return Result::err("driver_error");
    }
    ctx_.freq_tx = *f;
    return Result::okFloat(static_cast<double>(*f));
  }

  Result onGet() override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    return Result::okFloat(static_cast<double>(ctx_.freq_tx));
  }

private:
  Sa818Context &ctx_;
};

class RxFrequencyCap : public Setting {
public:
  explicit RxFrequencyCap(Sa818Context &ctx) : ctx_(ctx) {}
  const FieldSpec &spec() const override { return RXFREQ_SPEC; }

protected:
  Result onSet(const char *value) override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    std::optional<float> f = parse_float(value);
    if (!f) {
      return Result::err("bad_value");
    }
    if (!RXFREQ_SPEC.inAnyRange(static_cast<double>(*f))) {
      return Result::err("out_of_range");
    }
    if (sa818_at_set_group(ctx_.dev, ctx_.bw, ctx_.freq_tx, *f, ctx_.tone_tx, ctx_.squelch, ctx_.tone_rx) != SA818_OK) {
      return Result::err("driver_error");
    }
    ctx_.freq_rx = *f;
    return Result::okFloat(static_cast<double>(*f));
  }

  Result onGet() override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    return Result::okFloat(static_cast<double>(ctx_.freq_rx));
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
    if (sa818_at_set_group(ctx_.dev, bw, ctx_.freq_tx, ctx_.freq_rx, ctx_.tone_tx, ctx_.squelch, ctx_.tone_rx) != SA818_OK) {
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
    if (!VOLUME_SPEC.inAnyRange(static_cast<double>(*v))) {
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

class BandCap : public Telemetry {
public:
  explicit BandCap(Sa818Context &ctx) : ctx_(ctx) {}
  const FieldSpec &spec() const override { return BAND_SPEC; }

protected:
  Result onGet() override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    double f = static_cast<double>(ctx_.freq_rx);
    for (size_t i = 0; i < FREQ_SPEC.rangeCount; ++i) {
      if (FREQ_SPEC.ranges[i].name != nullptr && f >= FREQ_SPEC.ranges[i].min && f <= FREQ_SPEC.ranges[i].max) {
        return Result::okStr(FREQ_SPEC.ranges[i].name); // range name is a static literal
      }
    }
    return Result::okNull();
  }

private:
  Sa818Context &ctx_;
};

class TxToneCap : public Setting {
public:
  explicit TxToneCap(Sa818Context &ctx) : ctx_(ctx) {}
  const FieldSpec &spec() const override { return TXTONE_SPEC; }

protected:
  Result onSet(const char *value) override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    sa818_tone_code t = sa818_at_parse_tone(value);
    if (sa818_at_set_group(ctx_.dev, ctx_.bw, ctx_.freq_tx, ctx_.freq_rx, t, ctx_.squelch, ctx_.tone_rx) != SA818_OK) {
      return Result::err("driver_error");
    }
    ctx_.tone_tx = t;
    char b[16] = {0};
    (void)sa818_at_tone_to_str(t, b, sizeof(b));
    return Result::okStrCopy(b);
  }
  Result onGet() override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    char b[16] = {0};
    (void)sa818_at_tone_to_str(ctx_.tone_tx, b, sizeof(b));
    return Result::okStrCopy(b);
  }

private:
  Sa818Context &ctx_;
};

class RxToneCap : public Setting {
public:
  explicit RxToneCap(Sa818Context &ctx) : ctx_(ctx) {}
  const FieldSpec &spec() const override { return RXTONE_SPEC; }

protected:
  Result onSet(const char *value) override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    sa818_tone_code t = sa818_at_parse_tone(value);
    if (sa818_at_set_group(ctx_.dev, ctx_.bw, ctx_.freq_tx, ctx_.freq_rx, ctx_.tone_tx, ctx_.squelch, t) != SA818_OK) {
      return Result::err("driver_error");
    }
    ctx_.tone_rx = t;
    char b[16] = {0};
    (void)sa818_at_tone_to_str(t, b, sizeof(b));
    return Result::okStrCopy(b);
  }
  Result onGet() override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    char b[16] = {0};
    (void)sa818_at_tone_to_str(ctx_.tone_rx, b, sizeof(b));
    return Result::okStrCopy(b);
  }

private:
  Sa818Context &ctx_;
};

class SquelchCap : public Setting {
public:
  explicit SquelchCap(Sa818Context &ctx) : ctx_(ctx) {}
  const FieldSpec &spec() const override { return SQUELCH_SPEC; }

protected:
  Result onSet(const char *value) override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    std::optional<long> v = parse_int(value);
    if (!v) {
      return Result::err("bad_value");
    }
    if (!SQUELCH_SPEC.inAnyRange(static_cast<double>(*v))) {
      return Result::err("out_of_range");
    }
    sa818_squelch_level sq = static_cast<sa818_squelch_level>(*v);
    if (sa818_at_set_group(ctx_.dev, ctx_.bw, ctx_.freq_tx, ctx_.freq_rx, ctx_.tone_tx, sq, ctx_.tone_rx) != SA818_OK) {
      return Result::err("driver_error");
    }
    ctx_.squelch = sq;
    return Result::okInt(static_cast<int>(*v));
  }

  Result onGet() override {
    if (!ctx_.ready()) {
      return Result::err("driver_error");
    }
    return Result::okInt(static_cast<int>(ctx_.squelch));
  }

private:
  Sa818Context &ctx_;
};

/* Registry: one shared context + one instance per capability, all statically allocated. */
Sa818Context g_ctx{DEVICE_DT_GET_OR_NULL(DT_NODELABEL(sa818)), SA818_BW_12_5_KHZ, 145.500f, 145.500f, SA818_TONE_NONE, SA818_TONE_NONE, SA818_SQL_LEVEL_4};

FrequencyCap g_freq{g_ctx};
TxFrequencyCap g_txfreq{g_ctx};
RxFrequencyCap g_rxfreq{g_ctx};
PttCap g_ptt{g_ctx};
PowerLevelCap g_power{g_ctx};
RssiCap g_rssi{g_ctx};
VolumeCap g_volume{g_ctx};
BandwidthCap g_bandwidth{g_ctx};
SquelchCap g_squelch{g_ctx};
TxToneCap g_txtone{g_ctx};
RxToneCap g_rxtone{g_ctx};
BandCap g_band{g_ctx};

Capability *const g_caps[] = {&g_freq, &g_txfreq, &g_rxfreq, &g_ptt, &g_power, &g_rssi, &g_volume, &g_bandwidth, &g_squelch, &g_txtone, &g_rxtone, &g_band};
const Identity g_identity{"fm_transceiver", BAND_MODEL, BAND_NAME};
Module g_module{g_identity, "fm", g_caps};
Module *const g_modules[] = {&g_module};
ModuleRegistry g_registry{g_modules};

void emit_result(const struct shell *sh, const Result &r, const char *module, const char *cap, Op op) {
  char buf[RESULT_BUF_SIZE];
  mod::JsonWriter w(buf, sizeof(buf));
  w.raw("MODULE-RESULT ");
  r.render(w, module, cap, mod::opStr(op));
  if (w.truncated()) {
    // Pathologically long cap/value: fall back to a guaranteed-valid short result
    // rather than emit truncated (invalid) JSON. op is a short internal literal.
    shell_print(sh,
                "MODULE-RESULT {\"ok\":false,\"module\":\"\",\"cap\":\"\",\"op\":\"%s\","
                "\"error\":\"too_long\"}",
                mod::opStr(op));
    return;
  }
  shell_print(sh, "%s", w.c_str());
}

int cmd_module(const struct shell *sh, size_t argc, char **argv) {
  if (argc >= 2 && !strcmp(argv[1], "list")) {
    char buf[RESULT_BUF_SIZE];
    mod::JsonWriter w(buf, sizeof(buf));
    w.raw("MODULE-LIST ");
    g_registry.list(w);
    if (w.truncated()) {
      shell_print(sh, "MODULE-LIST {\"modules\":[]}");
      return 0;
    }
    shell_print(sh, "%s", w.c_str());
    return 0;
  }
  if (argc < 3) {
    emit_result(sh, Result::err("usage"), argc >= 2 ? argv[1] : "", "", Op::Get);
    return 0;
  }

  const char *id = argv[1];
  const char *op = argv[2];
  Module *m = g_registry.find(id);

  if (!strcmp(op, "describe")) {
    if (m == nullptr) {
      emit_result(sh, Result::err("unknown_module"), id, "", Op::Get);
      return 0;
    }
    char buf[DESCRIBE_BUF_SIZE];
    mod::JsonWriter w(buf, sizeof(buf));
    w.raw("MODULE-DESCRIBE ");
    m->describe(w);
    if (w.truncated()) {
      // Descriptor outgrew the buffer: emit a minimal valid descriptor rather than
      // truncated (invalid) JSON, so the machine-readable contract still holds.
      shell_print(sh, "MODULE-DESCRIBE {\"schema\":1,\"error\":\"too_long\"}");
      return 0;
    }
    shell_print(sh, "%s", w.c_str());
    return 0;
  }

  if (!strcmp(op, "set")) {
    if (argc < 5) {
      emit_result(sh, Result::err("usage"), id, argc >= 4 ? argv[3] : "", Op::Set);
      return 0;
    }
    const char *cap = argv[3];
    const char *value = argv[4];
    Result r = (m != nullptr) ? m->execute(Op::Set, cap, value) : Result::err("unknown_module");
    emit_result(sh, r, id, cap, Op::Set);
    return 0;
  }

  if (!strcmp(op, "get")) {
    if (argc < 4) {
      emit_result(sh, Result::err("usage"), id, "", Op::Get);
      return 0;
    }
    const char *cap = argv[3];
    Result r = (m != nullptr) ? m->execute(Op::Get, cap, "") : Result::err("unknown_module");
    emit_result(sh, r, id, cap, Op::Get);
    return 0;
  }

  if (!strcmp(op, "do")) {
    if (argc < 5) {
      emit_result(sh, Result::err("usage"), id, argc >= 4 ? argv[3] : "", Op::Do);
      return 0;
    }
    const char *cap = argv[3];
    const char *value = argv[4];
    Result r = (m != nullptr) ? m->execute(Op::Do, cap, value) : Result::err("unknown_module");
    emit_result(sh, r, id, cap, Op::Do);
    return 0;
  }

  emit_result(sh, Result::err("usage"), id, "", Op::Get);
  return 0;
}

} // namespace

SHELL_CMD_REGISTER(module, NULL, "module list | module <id> describe|set|get|do <cap> [value]", cmd_module);

#endif /* CONFIG_MODULE_SA818 */
