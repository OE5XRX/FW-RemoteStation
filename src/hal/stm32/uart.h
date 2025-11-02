#ifndef UART_H_
#define UART_H_

#include "stm32f3xx_hal.h"

#include <FreeRTOS/Queue.hpp>
#include <FreeRTOS/Task.hpp>
#include <cstdint>

#include "../../ring_buffer.h"

constexpr uint32_t UART_STACK_SIZE = 4 * configMINIMAL_STACK_SIZE;

class Uart : public FreeRTOS::StaticTask<UART_STACK_SIZE> {
public:
  Uart(FreeRTOS::QueueBase<char> *inputQueue, FreeRTOS::QueueBase<char> *outputQueue);

  // Statische Instanz für C-Callbacks
  static Uart       *s_instance;
  UART_HandleTypeDef _huart;

private:
  void taskFunction() final;
  void initSerial();

  // Hilfsfunktionen für TX/RX
  void startTxIfIdle();
  void onTxComplete();
  void onRxByte(uint8_t b);

  // Queues für Kommunikation
  FreeRTOS::QueueBase<char> *_inputQueue;
  FreeRTOS::QueueBase<char> *_outputQueue;

  // RX Buffer mit ISR-Protection (wird aus ISR beschrieben)
  RingBuffer<uint8_t, 256, IsrCrit> _rxBuffer;

  // TX Buffer mit RTOS-Protection (wird aus Task beschrieben)
  RingBuffer<uint8_t, 256, RtosCrit> _txBuffer;

  volatile bool _txBusy = false;

  // Friend-Deklarationen für C-Callbacks
  friend void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
  friend void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
};

#endif
