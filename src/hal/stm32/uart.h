#ifndef UART_H_
#define UART_H_

#include "stm32f3xx_hal.h"

#include "../../ring_buffer.h"
#include "../interface_uart.h"

class Uart : public iUart {
public:
  Uart(FreeRTOS::QueueBase<LINE_STRING> *inputQueue, FreeRTOS::QueueBase<LINE_STRING> *outputQueue);

  UART_HandleTypeDef _huart;

private:
  void taskFunction() final;
  void initSerial();

  void startTxIfIdle();
  void onTxComplete();
  void onRxByte(uint8_t b);

  RingBuffer<uint8_t, 256, IsrCrit>  _rxBuffer;
  RingBuffer<uint8_t, 256, RtosCrit> _txBuffer;

  LINE_STRING _rxLine;
  LINE_STRING _txLine;
  size_t      _txLinePos = 0;

  volatile bool _txBusy = false;

  friend void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
  friend void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);

  Uart(const Uart &)            = delete;
  Uart &operator=(const Uart &) = delete;
};

#endif
