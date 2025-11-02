#include "uart.h"
#include "../../shell/log.h"
#include "stm32f3xx_hal.h"

Uart *Uart::s_instance = nullptr;

Uart::Uart(FreeRTOS::QueueBase<char> *inputQueue, FreeRTOS::QueueBase<char> *outputQueue) : StaticTask<UART_STACK_SIZE>(tskIDLE_PRIORITY + 2, "UartTask"), _inputQueue(inputQueue), _outputQueue(outputQueue) {
  logger.assert(s_instance == nullptr, "Only one Uart instance supported");
  s_instance = this;
  initSerial();
}

void Uart::taskFunction() {
  // Starte Byteweises RX per Interrupt
  static uint8_t rxByte;
  HAL_UART_Receive_IT(&_huart, &rxByte, 1);

  while (true) {
    // 1) RX Buffer -> Input Queue
    while (auto rxByte = _rxBuffer.try_pop()) {
      _inputQueue->sendToBack(static_cast<char>(*rxByte));
    }

    // 2) Output Queue -> TX Buffer
    while (!_txBuffer.full() && _outputQueue->isValid()) {
      auto txOpt = _outputQueue->receive(0);
      if (!txOpt.has_value())
        break;

      _txBuffer.try_push(static_cast<uint8_t>(*txOpt));
    }

    // 3) TX Buffer -> UART (falls nicht busy)
    startTxIfIdle();

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void Uart::startTxIfIdle() {
  if (_txBusy || _txBuffer.empty())
    return;

  // Hole nächstes Byte aus TX Buffer
  auto nextByte = _txBuffer.try_pop();
  if (!nextByte.has_value())
    return;

  // Starte non-blocking TX
  uint8_t txByte           = *nextByte;
  _txBusy                  = true;
  HAL_StatusTypeDef status = HAL_UART_Transmit_IT(&_huart, &txByte, 1);

  if (status != HAL_OK) {
    _txBusy = false;
    _txBuffer.try_push(txByte); // Zurück in Buffer
  }
}

void Uart::onTxComplete() {
  _txBusy = false;
  startTxIfIdle(); // Nächstes Byte falls vorhanden
}

void Uart::onRxByte(uint8_t b) {
  _rxBuffer.try_push(b);
}

void Uart::initSerial() {
  /* GPIO für USART1 (PB6 = TX, PB7 = RX) */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitTypeDef SerialGpio;
  SerialGpio.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
  SerialGpio.Mode      = GPIO_MODE_AF_PP;
  SerialGpio.Pull      = GPIO_PULLUP;
  SerialGpio.Speed     = GPIO_SPEED_FREQ_LOW;
  SerialGpio.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(GPIOB, &SerialGpio);

  /* USART1 Clock Setup */
  RCC_PeriphCLKInitTypeDef PeriphClk;
  PeriphClk.PeriphClockSelection = RCC_PERIPHCLK_USART1;
  PeriphClk.Usart1ClockSelection = RCC_USART1CLKSOURCE_SYSCLK;
  HAL_StatusTypeDef clkStatus    = HAL_RCCEx_PeriphCLKConfig(&PeriphClk);
  logger.assert(clkStatus == HAL_OK, "PeriphCLKConfig");

  /* UART Setup */
  __HAL_RCC_USART1_CLK_ENABLE();

  _huart.Instance          = USART1;
  _huart.Init.BaudRate     = 9600;
  _huart.Init.WordLength   = UART_WORDLENGTH_8B;
  _huart.Init.StopBits     = UART_STOPBITS_1;
  _huart.Init.Parity       = UART_PARITY_NONE;
  _huart.Init.Mode         = UART_MODE_TX_RX;
  _huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  _huart.Init.OverSampling = UART_OVERSAMPLING_16;

  HAL_StatusTypeDef status = HAL_UART_Init(&_huart);
  logger.assert(status == HAL_OK, "UART_Init");

  /* RX Timeout */
  USART1->RTOR = (20 << USART_RTOR_RTO_Pos) & USART_RTOR_RTO_Msk;

  /* NVIC Setup */
  NVIC_SetPriority(USART1_IRQn, 3);
  NVIC_EnableIRQ(USART1_IRQn);
}

// C-Callbacks
extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
  if (Uart::s_instance && huart == &Uart::s_instance->_huart) {
    Uart::s_instance->onTxComplete();
  }
}

extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  if (!Uart::s_instance || huart != &Uart::s_instance->_huart)
    return;

  // Das empfangene Byte aus dem HAL Buffer holen
  uint8_t rxByte = *(reinterpret_cast<uint8_t *>(huart->pRxBuffPtr) - (huart->RxXferCount ? 0 : 1));

  Uart::s_instance->onRxByte(rxByte);

  // Nächsten Empfang starten
  static uint8_t nextByte;
  HAL_UART_Receive_IT(huart, &nextByte, 1);
}

extern "C" void USART1_IRQHandler(void) {
  HAL_UART_IRQHandler(&(Uart::s_instance->_huart));
}
