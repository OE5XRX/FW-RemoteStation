#ifndef STM32_CDC_H_
#define STM32_CDC_H_

#include <FreeRTOS/Queue.hpp>
#include <FreeRTOS/Task.hpp>
#include <cstdint>

#include "../../cdc_base.h"

class CdcTask : public CdcBase {
public:
  CdcTask();

private:
  ssize_t readBytes(uint8_t *buf, size_t maxlen, uint32_t /*timeout_ms*/) override;
  void    flushOutput() override;
};

extern CdcTask cdcTask;

#include "../../../shell/shell.h"
extern Shell shell;

#endif
