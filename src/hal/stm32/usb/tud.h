#ifndef TUD_H_
#define TUD_H_

#include <tusb_config.h>

#include <FreeRTOS/Task.hpp>

constexpr uint32_t USBD_STACK_SIZE = (2 * configMINIMAL_STACK_SIZE) * (CFG_TUSB_DEBUG ? 2 : 1);

class TudTask : public FreeRTOS::StaticTask<USBD_STACK_SIZE> {
public:
  TudTask();

private:
  void taskFunction() final;

  void init_clk_gpio();
};

#endif
