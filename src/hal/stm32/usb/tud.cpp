#include <stm32f3xx_hal.h>
#include <tusb_config.h>

#include <tusb.h>

#include "tud.h"

TudTask::TudTask() : FreeRTOS::StaticTask<USBD_STACK_SIZE>(configMAX_PRIORITIES - 1, "usbd") {
}

void TudTask::taskFunction() {
  init_clk_gpio();
  tud_init(BOARD_TUD_RHPORT);

  while (1) {
    tud_task();
  }
}

void TudTask::init_clk_gpio() {
  __HAL_REMAPINTERRUPT_USB_ENABLE();

  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitTypeDef GPIO_InitStruct;
  GPIO_InitStruct.Pin       = (GPIO_PIN_11 | GPIO_PIN_12);
  GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull      = GPIO_NOPULL;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF14_USB;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  __HAL_RCC_USB_CLK_ENABLE();
}

static TudTask tudTask;
