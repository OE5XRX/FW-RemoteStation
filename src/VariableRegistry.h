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

  std::size_t size() const;

  const char   *getName(std::size_t index) const;
  VariableBase *getVar(std::size_t index) const;

  VariableRegistry(const VariableRegistry &)            = delete;
  VariableRegistry &operator=(const VariableRegistry &) = delete;

private:
  VariableRegistry();

  static constexpr std::size_t MAX_VARIABLES = 32;

  VariableBase *_variables[MAX_VARIABLES];
  std::size_t   _count;
};

#include "VariableHelpers.h"

template <typename T> class Variable : public VariableBase {
public:
  Variable(const char *n, T &target) : _name(n), _ref(target) {
    VariableRegistry::instance().registerVar(this);
  }

  const char *name() const override {
    return _name;
  }

  bool setFromString(const char *str) override {
    return parseFromString<T>(_ref, str);
  }

  void getAsString(char *buffer, std::size_t bufferSize) const override {
    stringifyToBuffer<T>(_ref, buffer, bufferSize);
  }

  T &ref() {
    return _ref;
  }
  const T &ref() const {
    return _ref;
  }

private:
  const char *_name;
  T          &_ref;
};

#endif
