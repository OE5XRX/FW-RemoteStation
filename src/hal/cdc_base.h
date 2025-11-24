#ifndef HAL_CDC_BASE_H_
#define HAL_CDC_BASE_H_

#include <FreeRTOS/Task.hpp>
#include <cstddef>
#include <cstdint>

#include "../shell/log.h"
#include "../shell/shell.h"

enum class EscapeState {
  None,
  Escape,
  CSI
};

constexpr uint32_t CDC_STACK_SIZE = 2 * configMINIMAL_STACK_SIZE;

class CdcBase : public FreeRTOS::StaticTask<CDC_STACK_SIZE> {
public:
  CdcBase(UBaseType_t priority, const char *name);

protected:
  virtual ssize_t readBytes(uint8_t *buf, size_t maxlen, uint32_t timeout_ms) = 0;
  virtual void    flushOutput()                                               = 0;

  virtual void terminalInit();
  virtual void terminalRestore();

  void taskFunction() final;

  EscapeState _escState;
};

#endif
