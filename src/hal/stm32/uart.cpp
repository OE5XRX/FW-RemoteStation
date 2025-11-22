#include "uart.h"
#include "../../shell/log.h"

#include <optional>

static uint8_t s_rxByte = 0;
static uint8_t s_txByte = 0;

Uart::Uart(FreeRTOS::QueueBase<LINE_STRING> *inputQueue, FreeRTOS::QueueBase<LINE_STRING> *outputQueue) : iUart(inputQueue, outputQueue) {
  iUart::registerInstance(this);
}

void Uart::taskFunction() {
  initSerial();

  // Starte Byteweises RX per Interrupt (einmalig)
  HAL_UART_Receive_IT(&_huart, &s_rxByte, 1);

  while (true) {
    // 1) RX Buffer -> Zeilen bilden -> Input Queue
    while (auto rb = _rxBuffer.try_pop()) {
      uint8_t b = *rb;
      if (b == '\n' || b == '\r') {
        if (_rxLine.length() > 0) {
          _inputQueue->sendToBack(_rxLine);
          _rxLine.clear();
        } else {
          LINE_STRING empty;
          _inputQueue->sendToBack(empty);
        }
      } else {
        // Normales Zeichen anhängen (falls Platz)
        if (_rxLine.length() < CLI_MAX_LINE_LENGTH) {
          _rxLine.append(static_cast<char>(b));
        } else {
          // Zeile voll -> abschicken und neuen Beginn mit aktuellem Zeichen
          _inputQueue->sendToBack(_rxLine);
          _rxLine.clear();
          _rxLine.append(static_cast<char>(b));
        }
      }
    }

    // 2) Output Queue (LINE_STRING) -> TX RingBuffer (byteweise)
    while (!_txBuffer.full() && _outputQueue->isValid()) {
      auto optLine = _outputQueue->receive(0);
      if (!optLine.has_value())
        break;

      LINE_STRING line = *optLine;
      // Versuche alle Bytes der Linie in den TX-Ring zu pushen.
      size_t i = 0;
      for (; i < line.length(); ++i) {
        if (!_txBuffer.try_push(static_cast<uint8_t>(line.get(i)))) {
          break; // ring voll
        }
      }

      if (i < line.length()) {
        // Rest der Linie zurück in die Output-Queue (ans Ende)
        LINE_STRING rem;
        for (size_t j = i; j < line.length(); ++j) {
          rem.append(line.get(j));
        }
        _outputQueue->sendToBack(rem);
        vTaskDelay(pdMS_TO_TICKS(1));
        break; // TX-Ring voll, weiter in nächster Iteration
      }
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

  // Lege Byte in stabilen Speicher und starte non-blocking TX
  s_txByte = *nextByte;
  _txBusy  = true;

  HAL_StatusTypeDef status = HAL_UART_Transmit_IT(&_huart, &s_txByte, 1);

  if (status != HAL_OK) {
    // Bei Fehler zurück in Buffer
    _txBusy = false;
    _txBuffer.try_push(s_txByte);
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
  iUart *base = iUart::getInstance();
  if (base == nullptr) {
    return;
  }
  if (huart != &static_cast<Uart *>(base)->_huart) {
    return;
  }
  static_cast<Uart *>(base)->onTxComplete();
}

extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  iUart *base = iUart::getInstance();
  if (base == nullptr) {
    return;
  }
  if (huart != &static_cast<Uart *>(base)->_huart) {
    return;
  }

  // s_rxByte enthält das empfangene Byte (stabiler Speicher)
  static_cast<Uart *>(base)->onRxByte(s_rxByte);

  // Nächsten Empfang starten
  HAL_UART_Receive_IT(&static_cast<Uart *>(base)->_huart, &s_rxByte, 1);
}

extern "C" void USART1_IRQHandler(void) {
  iUart *base = iUart::getInstance();
  if (base == nullptr) {
    return;
  }
  HAL_UART_IRQHandler(&static_cast<Uart *>(base)->_huart);
}
