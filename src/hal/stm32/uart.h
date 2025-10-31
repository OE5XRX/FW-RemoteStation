#ifndef UART_H_
#define UART_H_

#include <FreeRTOS/Queue.hpp>
#include <FreeRTOS/Task.hpp>
#include <cstdint>

constexpr uint32_t UART_STACK_SIZE = 4 * configMINIMAL_STACK_SIZE;

class Uart : public FreeRTOS::StaticTask<UART_STACK_SIZE> {
public:
  Uart();

private:
  void taskFunction() final;

  void initSerial();
};

#endif
