#ifndef VARIABLE_REGISTRY_H_
#define VARIABLE_REGISTRY_H_

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

#include "fixed_string.h"

class VariableBase {
public:
  virtual ~VariableBase() = default;

  virtual const char *name() const                                            = 0;
  virtual bool        setFromString(const char *str)                          = 0;
  virtual void        getAsString(char *buffer, std::size_t bufferSize) const = 0;
};

class VariableRegistry {
public:
  static VariableRegistry &instance();

  void          registerVar(VariableBase *var);
  VariableBase *find(const char *name);

  bool set(const char *name, const char *value);
  bool get(const char *name, char *buffer, std::size_t bufferSize);

  const char *getName(std::size_t index) const;

  VariableRegistry(const VariableRegistry &)            = delete;
  VariableRegistry &operator=(const VariableRegistry &) = delete;

private:
  VariableRegistry();

  static constexpr std::size_t MAX_VARIABLES = 32;

  VariableBase *_variables[MAX_VARIABLES];
  std::size_t   _count;
};

inline VariableRegistry &variableRegistry = VariableRegistry::instance();

template <typename T> class Variable : public VariableBase {
public:
  Variable(const char *n, T initial) : _name(n), _value(initial) {
    variableRegistry.registerVar(this);
  }

  const char *name() const override {
    return _name;
  }

  bool setFromString(const char *str) override {
    if constexpr (std::is_same_v<T, int>) {
      _value = std::atoi(str);
    } else if constexpr (std::is_same_v<T, float>) {
      _value = static_cast<float>(std::atof(str));
    } else if constexpr (std::is_same_v<T, double>) {
      _value = std::atof(str);
    } else if constexpr (std::is_same_v<T, bool>) {
      if (std::strcmp(str, "1") == 0 || std::strcmp(str, "true") == 0) {
        _value = true;
      } else {
        _value = false;
      }
    } else {
      return false;
    }
    return true;
  }

  void getAsString(char *buffer, std::size_t bufferSize) const override {
    if (bufferSize == 0)
      return;

    if constexpr (std::is_same_v<T, int>) {
      std::snprintf(buffer, bufferSize, "%d", _value);
    } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
      std::snprintf(buffer, bufferSize, "%.6f", static_cast<double>(_value));
    } else if constexpr (std::is_same_v<T, bool>) {
      std::snprintf(buffer, bufferSize, "%s", _value ? "true" : "false");
    } else {
      std::snprintf(buffer, bufferSize, "<unsupported>");
    }
  }

  T &ref() {
    return _value;
  }
  const T &ref() const {
    return _value;
  }

private:
  const char *_name;
  T           _value;
};

#endif
