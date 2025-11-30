#include "system_manager.h"

#if (defined(UNITTEST_BUILD) || defined(HOST_BUILD))
#include "hal/host/cat24c32_sim.h"
#else
#include "hal/stm32/cat24c32_stm32.h"
#endif

CAT24C325 *SystemManager::_eeprom = nullptr;

void SystemManager::init() {
#if (defined(UNITTEST_BUILD) || defined(HOST_BUILD))
  static CAT24C325_Sim s_eeprom_sim;
  _eeprom = &s_eeprom_sim;
#else
  static CAT24C325_stm32 s_eeprom_hw{nullptr};
  _eeprom = &s_eeprom_hw;
#endif
}

CAT24C325 &SystemManager::getEeprom() {
  return *_eeprom;
}
