#ifndef UART_H_
#define UART_H_

#include "stm32f3xx_hal.h"

#include <FreeRTOS/Queue.hpp>
#include <FreeRTOS/Task.hpp>
#include <cstdint>

#include "../../fixed_string.h"
#include "../../ring_buffer.h"
#include "../../shell/constant.h"

constexpr uint32_t UART_STACK_SIZE = 4 * configMINIMAL_STACK_SIZE;

class Uart : public FreeRTOS::StaticTask<UART_STACK_SIZE> {
public:
  Uart(FreeRTOS::QueueBase<LINE_STRING> *inputQueue, FreeRTOS::QueueBase<LINE_STRING> *outputQueue);

  UART_HandleTypeDef _huart;

  static inline Uart *getInstance() {
    return s_instance;
  }

private:
  void taskFunction() final;
  void initSerial();

  // Hilfsfunktionen für TX/RX
  void startTxIfIdle();
  void onTxComplete();
  void onRxByte(uint8_t b);

  // Queues für Kommunikation
  FreeRTOS::QueueBase<LINE_STRING> *_inputQueue;
  FreeRTOS::QueueBase<LINE_STRING> *_outputQueue;

  RingBuffer<uint8_t, 256, IsrCrit>  _rxBuffer;
  RingBuffer<uint8_t, 256, RtosCrit> _txBuffer;

  LINE_STRING _rxLine;
  LINE_STRING _txLine;
  size_t      _txLinePos = 0;

  volatile bool _txBusy = false;

  static Uart *s_instance;

  friend void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
  friend void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);

  Uart(const Uart &)            = delete;
  Uart &operator=(const Uart &) = delete;
};

#endif
