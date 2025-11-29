#ifndef SYSTEM_SERVICES_H_
#define SYSTEM_SERVICES_H_

#include "cat24c32.h"
#include <cstddef>
#include <cstdint>

class SystemServices {
public:
  static CAT24C325 &getEeprom() noexcept;

  // Erweiterungspunkte (Beispiel, bei Bedarf hinzufügen):
  // static void registerUart(UartInterface* inst) noexcept;
  // static UartInterface& getUart() noexcept;
  // static bool hasUart() noexcept;

private:
  SystemServices()                                  = delete;
  SystemServices(const SystemServices &)            = delete;
  SystemServices &operator=(const SystemServices &) = delete;
};

#endif
