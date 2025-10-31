
#include "stm32f3xx_hal.h"

#include "../../shell/log.h"

#include "uart.h"

Uart::Uart() {
}

void Uart::taskFunction() {
}

void Uart::initSerial() {
  /* Set up GPIO */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitTypeDef SerialGpio;
  SerialGpio.Pin       = (GPIO_PIN_6 | GPIO_PIN_7);
  SerialGpio.Mode      = GPIO_MODE_AF_PP;
  SerialGpio.Pull      = GPIO_PULLUP;
  SerialGpio.Speed     = GPIO_SPEED_FREQ_LOW;
  SerialGpio.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(GPIOB, &SerialGpio);

  /* Errata 2.11.5 When PCLK is selected as clock source for USART1, PCLK1 is used instead of PCLK2.
   *  To reach 9 Mbaud, System Clock (SYSCLK) should be selected as USART1 clock source. */
  RCC_PeriphCLKInitTypeDef PeriphClk;
  PeriphClk.PeriphClockSelection = RCC_PERIPHCLK_USART1;
  PeriphClk.Usart1ClockSelection = RCC_USART1CLKSOURCE_SYSCLK;
  HAL_StatusTypeDef status       = HAL_RCCEx_PeriphCLKConfig(&PeriphClk);
  logger.assert(status == HAL_OK, "SerialGpio");

  /* Initialize UART */
  __HAL_RCC_USART1_CLK_ENABLE();
  USART1->CR1  = USART_CR1_RTOIE | UART_OVERSAMPLING_16 | UART_WORDLENGTH_8B | UART_PARITY_NONE | USART_CR1_RXNEIE | UART_MODE_RX; /* Enable receiver only, transmitter will be enabled on-demand */
  USART1->CR2  = UART_RECEIVER_TIMEOUT_ENABLE | UART_STOPBITS_1;
  USART1->CR3  = USART_CR3_EIE;
  USART1->BRR  = (HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_USART1) + 9600 / 2) / 9600;
  USART1->RTOR = (20 << USART_RTOR_RTO_Pos) & USART_RTOR_RTO_Msk;
  USART1->CR1 |= USART_CR1_UE;

  /* Enable interrupt */
  NVIC_SetPriority(USART1_IRQn, 3);
  NVIC_EnableIRQ(USART1_IRQn);
}
