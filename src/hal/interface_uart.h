#ifndef INTERFACE_UART_H_
#define INTERFACE_UART_H_

#include <FreeRTOS/Queue.hpp>
#include <FreeRTOS/Task.hpp>
#include <cstdint>

#include "../fixed_string.h"
#include "../ring_buffer.h"
#include "../shell/constant.h"
#include "../shell/log.h"

constexpr UBaseType_t UART_STACK_SIZE = 3 * configMINIMAL_STACK_SIZE;

class iUart : public FreeRTOS::StaticTask<UART_STACK_SIZE> {
public:
  iUart(FreeRTOS::QueueBase<LINE_STRING> *inputQueue, FreeRTOS::QueueBase<LINE_STRING> *outputQueue) : StaticTask<UART_STACK_SIZE>(tskIDLE_PRIORITY + 2, "UartTask"), _inputQueue(inputQueue), _outputQueue(outputQueue) {
  }
  virtual ~iUart() {
    unregisterInstance(this);
  }

  static inline void registerInstance(iUart *inst) {
    s_instance = inst;
  }
  static inline void unregisterInstance(iUart *inst) {
    if (s_instance == inst)
      s_instance = nullptr;
  }
  static inline iUart *getInstance() {
    return s_instance;
  }

protected:
  FreeRTOS::QueueBase<LINE_STRING> *_inputQueue;
  FreeRTOS::QueueBase<LINE_STRING> *_outputQueue;

private:
  virtual void taskFunction() = 0;

  inline static iUart *s_instance = nullptr;

  iUart(const iUart &)            = delete;
  iUart &operator=(const iUart &) = delete;
};

#endif
