#ifndef SYSTEM_MANAGER_H_
#define SYSTEM_MANAGER_H_

#include <cstddef>
#include <cstdint>

#include "cat24c32.h"

class SystemManager {
public:
  static void init();

  static CAT24C325 &getEeprom();

private:
  static CAT24C325 *_eeprom;

  SystemManager()                                 = delete;
  SystemManager(const SystemManager &)            = delete;
  SystemManager &operator=(const SystemManager &) = delete;
};

#endif
