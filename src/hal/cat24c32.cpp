#include "cat24c32.h"

#if (defined(UNITTEST_BUILD) || defined(HOST_BUILD))
#include "host/cat24c32_sim.h"
#else
#include "stm32/cat24c32_stm32.h"
#endif

CAT24C325 &CAT24C325::instance() {
#if (defined(UNITTEST_BUILD) || defined(HOST_BUILD))
  static CAT24C325_Sim inst;
#else
  static CAT24C325_stm32 inst(nullptr);
#endif
  return inst;
}
