#ifndef CDC_H_
#define CDC_H_

#include <FreeRTOS/Queue.hpp>
#include <FreeRTOS/Task.hpp>
#include <cstdint>

constexpr uint32_t CDC_STACK_SIZE = 2 * configMINIMAL_STACK_SIZE;

class CdcTask : public FreeRTOS::StaticTask<CDC_STACK_SIZE> {
public:
  CdcTask();

private:
  void taskFunction() final;
};

extern CdcTask cdcTask;

#include "../../../shell/shell.h"

extern Shell shell;

#endif
