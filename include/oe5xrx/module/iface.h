/**
 * @file module_iface.h
 * @brief Generic, device-agnostic module capability framework.
 *
 * A small object model for the Firmware<->Agent contract (module-platform meta-spec
 * §8). A concrete module is a set of @ref Capability objects, each of which owns BOTH
 * its typed descriptor metadata (rendered generically into the `describe` JSON) and its
 * set/get/do behavior. Capabilities inherit from one of the kind mixins (@ref Setting,
 * @ref Action, @ref Telemetry), which fix the capability kind and the op<->kind gating.
 * A @ref Module is a registry of capabilities plus identity; it renders `describe` and
 * dispatches `execute` as machine-readable JSON.
 *
 * This header has NO device / RTOS dependencies -- concrete, device-specific capabilities
 * live in the driver translation unit. Adding a capability is one new subclass + one
 * registry entry; a whole new module type reuses this framework unchanged.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#ifndef OE5XRX_MODULE_IFACE_H_
#define OE5XRX_MODULE_IFACE_H_

#ifndef __cplusplus
#error "oe5xrx/module/iface.h is a C++ header (namespaces/classes); include it only from C++."
#endif

#include <math.h>
#include <span>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace mod {

enum class Kind { Setting, Action, Telemetry };
enum class ValueType { Bool, Int, Float, Enum, String };
enum class Op { Set, Get, Do };

inline const char *kindStr(Kind k) {
  switch (k) {
  case Kind::Setting:
    return "setting";
  case Kind::Action:
    return "action";
  case Kind::Telemetry:
    return "telemetry";
  }
  return "";
}

inline const char *typeStr(ValueType t) {
  switch (t) {
  case ValueType::Bool:
    return "bool";
  case ValueType::Int:
    return "int";
  case ValueType::Float:
    return "float";
  case ValueType::Enum:
    return "enum";
  case ValueType::String:
    return "string";
  }
  return "";
}

inline const char *opStr(Op o) {
  switch (o) {
  case Op::Set:
    return "set";
  case Op::Get:
    return "get";
  case Op::Do:
    return "do";
  }
  return "";
}

/** A numeric constraint range; a named range is a band (e.g. "vhf", "20m"). */
struct Range {
  const char *name; // optional band label; nullptr for a plain scalar range
  double min;
  double max;
};

/**
 * @brief Static, typed descriptor of one capability.
 *
 * The single source of truth that @ref Capability::describe turns into JSON, so the
 * descriptor and the accepted input domain can never drift into separate string literals.
 */
struct FieldSpec {
  const char *name;
  ValueType type;
  const char *unit = nullptr;
  const Range *ranges = nullptr;
  size_t rangeCount = 0;
  const char *const *enumValues = nullptr;
  size_t enumCount = 0;
  bool readonly = false;
  const char *access = "operator";

  bool inAnyRange(double v) const {
    for (size_t i = 0; i < rangeCount; ++i) {
      if (v >= ranges[i].min && v <= ranges[i].max) {
        return true;
      }
    }
    return false;
  }
};

/** @brief Bounded, truncation-safe JSON string builder with string escaping. */
class JsonWriter {
public:
  JsonWriter(char *buf, size_t cap) : buf_(buf), cap_(cap) {
    if (cap_ > 0) {
      buf_[0] = '\0';
    }
  }

  void ch(char c) {
    if (len_ + 1 < cap_) {
      buf_[len_++] = c;
      buf_[len_] = '\0';
    } else {
      truncated_ = true;
    }
  }

  void raw(const char *s) {
    for (; s != nullptr && *s != '\0'; ++s) {
      ch(*s);
    }
  }

  /** Append @p s escaped as a JSON string body (no surrounding quotes). */
  void escaped(const char *s) {
    for (; s != nullptr && *s != '\0'; ++s) {
      unsigned char c = static_cast<unsigned char>(*s);
      if (c == '"' || c == '\\') {
        ch('\\');
        ch(static_cast<char>(c));
      } else if (c < 0x20) {
        if (len_ + 6 >= cap_) { // reserve room for a whole \uXXXX escape
          truncated_ = true;
          return;
        }
        char b[8];
        snprintf(b, sizeof(b), "\\u%04x", c);
        raw(b);
      } else {
        ch(static_cast<char>(c));
      }
    }
  }

  void quoted(const char *s) {
    ch('"');
    escaped(s);
    ch('"');
  }

  void key(const char *k) {
    quoted(k);
    ch(':');
  }

  void kvStr(const char *k, const char *v) {
    key(k);
    quoted(v);
  }

  /** Append `"k":v` where @p v is already-valid JSON (number / bool / literal). */
  void kvRaw(const char *k, const char *v) {
    key(k);
    raw(v);
  }

  bool truncated() const { return truncated_; }
  const char *c_str() const { return buf_; }

private:
  char *buf_;
  size_t cap_;
  size_t len_ = 0;
  bool truncated_ = false;
};

/** @brief Outcome of a command: success carrying a typed value, or an error code. */
class Result {
public:
  static Result okInt(int v) {
    Result r(true);
    r.vk_ = VK::Int;
    r.i_ = v;
    return r;
  }
  static Result okFloat(double v) {
    Result r(true);
    r.vk_ = VK::Float;
    r.f_ = v;
    return r;
  }
  static Result okBool(bool v) {
    Result r(true);
    r.vk_ = VK::Bool;
    r.b_ = v;
    return r;
  }
  static Result okStr(const char *v) {
    Result r(true);
    r.vk_ = VK::Str;
    r.s_ = v;
    return r;
  }
  static Result err(const char *code) {
    Result r(false);
    r.err_ = code;
    return r;
  }

  /** Render `{"ok":..,"module":..,"cap":..,"op":..,"value"|"error":..}` into @p w. */
  void render(JsonWriter &w, const char *module, const char *cap, const char *op) const {
    w.ch('{');
    w.kvRaw("ok", ok_ ? "true" : "false");
    w.ch(',');
    w.kvStr("module", module);
    w.ch(',');
    w.kvStr("cap", cap);
    w.ch(',');
    w.kvStr("op", op);
    w.ch(',');
    if (ok_) {
      w.key("value");
      renderValue(w);
    } else {
      w.kvStr("error", err_ != nullptr ? err_ : "error");
    }
    w.ch('}');
  }

private:
  enum class VK { None, Int, Float, Bool, Str };

  explicit Result(bool ok) : ok_(ok) {}

  void renderValue(JsonWriter &w) const {
    char b[32];
    switch (vk_) {
    case VK::Int:
      snprintf(b, sizeof(b), "%d", i_);
      w.raw(b);
      break;
    case VK::Float:
      if (!isfinite(f_)) {
        // NaN/Inf are not valid JSON numbers; keep the contract valid.
        w.raw("null");
        break;
      }
      snprintf(b, sizeof(b), "%.4f", f_);
      w.raw(b);
      break;
    case VK::Bool:
      w.raw(b_ ? "true" : "false");
      break;
    case VK::Str:
      w.quoted(s_);
      break;
    case VK::None:
    default:
      w.raw("null");
      break;
    }
  }

  bool ok_;
  VK vk_ = VK::None;
  int i_ = 0;
  double f_ = 0.0;
  bool b_ = false;
  const char *s_ = nullptr;
  const char *err_ = nullptr;
};

/**
 * @brief Abstract capability: owns its descriptor metadata plus set/get/do behavior.
 *
 * Concrete capabilities inherit a kind mixin (@ref Setting / @ref Action / @ref Telemetry)
 * and override the hooks that apply to that kind. @ref handle enforces the op<->kind
 * contract before delegating, so a wrong op can never reach a hook.
 */
class Capability {
public:
  virtual ~Capability() = default;

  virtual const FieldSpec &spec() const = 0;
  virtual Kind kind() const = 0;
  const char *name() const { return spec().name; }

  /** Append this capability's descriptor object to @p w, generically from its FieldSpec. */
  void describe(JsonWriter &w) const {
    const FieldSpec &s = spec();
    w.ch('{');
    w.kvStr("name", s.name);
    w.ch(',');
    w.kvStr("kind", kindStr(kind()));
    w.ch(',');
    w.kvStr("type", typeStr(s.type));
    if (s.unit != nullptr) {
      w.ch(',');
      w.kvStr("unit", s.unit);
    }
    if (s.ranges != nullptr && s.rangeCount > 0) {
      w.ch(',');
      w.key("ranges");
      w.ch('[');
      char b[24];
      for (size_t i = 0; i < s.rangeCount; ++i) {
        if (i != 0) {
          w.ch(',');
        }
        w.ch('{');
        if (s.ranges[i].name != nullptr) {
          w.kvStr("name", s.ranges[i].name);
          w.ch(',');
        }
        numStr(b, sizeof(b), s.ranges[i].min, s.type);
        w.kvRaw("min", b);
        w.ch(',');
        numStr(b, sizeof(b), s.ranges[i].max, s.type);
        w.kvRaw("max", b);
        w.ch('}');
      }
      w.ch(']');
    }
    if (s.enumValues != nullptr && s.enumCount > 0) {
      w.ch(',');
      w.key("values");
      w.ch('[');
      for (size_t i = 0; i < s.enumCount; ++i) {
        if (i != 0) {
          w.ch(',');
        }
        w.quoted(s.enumValues[i]);
      }
      w.ch(']');
    }
    if (s.readonly) {
      w.ch(',');
      w.kvRaw("readonly", "true");
    }
    w.ch(',');
    w.kvStr("access", s.access);
    w.ch('}');
  }

  /**
   * @brief Dispatch an op, enforcing the op<->kind contract.
   *
   * Implemented by the kind mixins (@ref Setting / @ref Action / @ref Telemetry): each
   * routes the ops valid for its kind to the hooks and rejects the rest (`wrong_op`,
   * or `read_only` for a write to telemetry). A concrete capability never sees an op
   * that does not apply to its kind.
   */
  virtual Result handle(Op op, const char *value) = 0;

protected:
  /* Hooks a concrete capability overrides. A Setting overrides onSet + onGet; an Action
   * overrides onDo (+ onGet for read-back); a Telemetry overrides onGet. Unoverridden
   * hooks are never reached for a valid op (the mixin gates them). */
  virtual Result onSet(const char *) { return Result::err("wrong_op"); }
  virtual Result onDo(const char *) { return Result::err("wrong_op"); }
  virtual Result onGet() { return Result::err("wrong_op"); }

private:
  /** Render @p v as JSON: integer for Int, always-decimal for Float. */
  static void numStr(char *b, size_t n, double v, ValueType t) {
    if (t == ValueType::Int) {
      snprintf(b, n, "%d", static_cast<int>(v));
      return;
    }
    snprintf(b, n, "%g", v);
    if (strpbrk(b, ".eE") == nullptr) {
      size_t l = strlen(b);
      if (l + 2 < n) {
        b[l] = '.';
        b[l + 1] = '0';
        b[l + 2] = '\0';
      }
    }
  }
};

/** @brief Kind mixin: a writable setting. `set`->onSet, `get`->onGet, `do`->wrong_op. */
class Setting : public Capability {
public:
  Kind kind() const override { return Kind::Setting; }
  Result handle(Op op, const char *value) override {
    switch (op) {
    case Op::Set:
      return onSet(value);
    case Op::Get:
      return onGet();
    case Op::Do:
    default:
      return Result::err("wrong_op");
    }
  }
};

/** @brief Kind mixin: an action. `do`->onDo, `get`->onGet (current state), `set`->wrong_op. */
class Action : public Capability {
public:
  Kind kind() const override { return Kind::Action; }
  Result handle(Op op, const char *value) override {
    switch (op) {
    case Op::Do:
      return onDo(value);
    case Op::Get:
      return onGet();
    case Op::Set:
    default:
      return Result::err("wrong_op");
    }
  }
};

/** @brief Kind mixin: read-only telemetry. `get`->onGet, `set`->read_only, `do`->wrong_op. */
class Telemetry : public Capability {
public:
  Kind kind() const override { return Kind::Telemetry; }
  Result handle(Op op, const char *) override {
    switch (op) {
    case Op::Get:
      return onGet();
    case Op::Set:
      return Result::err("read_only");
    case Op::Do:
    default:
      return Result::err("wrong_op");
    }
  }
};

struct Identity {
  const char *type;
  const char *model;
  const char *version;
};

/** @brief A module: identity + a fixed registry of capabilities, rendered as JSON. */
class Module {
public:
  Module(const Identity &id, const char *moduleId, std::span<Capability *const> caps) : id_(id), moduleId_(moduleId), caps_(caps) {}

  const char *moduleId() const { return moduleId_; }

  Capability *find(const char *name) const {
    for (Capability *c : caps_) {
      if (strcmp(c->name(), name) == 0) {
        return c;
      }
    }
    return nullptr;
  }

  void describe(JsonWriter &w) const {
    w.ch('{');
    w.kvRaw("schema", "1");
    w.ch(',');
    w.kvStr("module", moduleId_);
    w.ch(',');
    w.key("identity");
    w.ch('{');
    w.kvStr("type", id_.type);
    w.ch(',');
    w.kvStr("model", id_.model);
    w.ch(',');
    w.kvStr("version", id_.version);
    w.ch('}');
    w.ch(',');
    w.key("capabilities");
    w.ch('[');
    bool first = true;
    for (Capability *c : caps_) {
      if (!first) {
        w.ch(',');
      }
      first = false;
      c->describe(w);
    }
    w.ch(']');
    w.ch('}');
  }

  /** Look up @p cap and dispatch @p op; unknown capability -> `unknown_capability`. */
  Result execute(Op op, const char *cap, const char *value) const {
    Capability *c = find(cap);
    return (c != nullptr) ? c->handle(op, value) : Result::err("unknown_capability");
  }

private:
  Identity id_;
  const char *moduleId_;
  std::span<Capability *const> caps_;
};

/** @brief A fixed set of modules addressable by id. */
class ModuleRegistry {
public:
  explicit ModuleRegistry(std::span<Module *const> modules) : modules_(modules) {}

  Module *find(const char *id) const {
    for (Module *m : modules_) {
      if (strcmp(m->moduleId(), id) == 0) {
        return m;
      }
    }
    return nullptr;
  }

  void list(JsonWriter &w) const {
    w.ch('{');
    w.key("modules");
    w.ch('[');
    bool first = true;
    for (Module *m : modules_) {
      if (!first) {
        w.ch(',');
      }
      first = false;
      w.quoted(m->moduleId());
    }
    w.ch(']');
    w.ch('}');
  }

private:
  std::span<Module *const> modules_;
};

} // namespace mod

#endif // OE5XRX_MODULE_IFACE_H_
