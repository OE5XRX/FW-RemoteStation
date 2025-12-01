#include "VariableRegistry.h"

VariableRegistry &VariableRegistry::instance() {
  static VariableRegistry inst;
  return inst;
}

VariableRegistry::VariableRegistry() : _count(0) {
  for (std::size_t i = 0; i < MAX_VARIABLES; ++i) {
    _variables[i] = nullptr;
  }
}

void VariableRegistry::registerVar(VariableBase *var) {
  if (_count < MAX_VARIABLES) {
    _variables[_count] = var;
    _count++;
  }
}

VariableBase *VariableRegistry::find(const char *name) {
  for (std::size_t i = 0; i < _count; ++i) {
    if (std::strcmp(_variables[i]->name(), name) == 0) {
      return _variables[i];
    }
  }
  return nullptr;
}

bool VariableRegistry::set(const char *name, const char *value) {
  VariableBase *v = find(name);
  if (!v)
    return false;
  return v->setFromString(value);
}

bool VariableRegistry::get(const char *name, char *buffer, std::size_t bufferSize) {
  VariableBase const *const v = find(name);
  if (!v)
    return false;
  v->getAsString(buffer, bufferSize);
  return true;
}

std::size_t VariableRegistry::size() const {
  return _count;
}

const char *VariableRegistry::getName(std::size_t index) const {
  if (index >= _count) {
    return nullptr;
  }
  return _variables[index]->name();
}

VariableBase *VariableRegistry::getVar(std::size_t index) const {
  if (index >= _count)
    return nullptr;
  return _variables[index];
}
