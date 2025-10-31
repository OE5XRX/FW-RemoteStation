#include "stm32f3xx_hal.h"

TIM_HandleTypeDef htim2;

#ifdef __cplusplus
extern "C" {
#endif

void vConfigureTimerForRunTimeStats(void) {
  HAL_StatusTypeDef status;
  (void)status;

  __HAL_RCC_TIM2_CLK_ENABLE();

  htim2.Instance               = TIM2;
  htim2.Init.Prescaler         = (HAL_RCC_GetPCLK1Freq() / 1000000UL) - 1; // 1 µs Takt
  htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim2.Init.Period            = 0xFFFFFFFF;
  htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  status = HAL_TIM_Base_Init(&htim2);
  UNUSED(status);

  HAL_TIM_Base_Start(&htim2);
}

uint32_t ulGetRunTimeCounterValue(void) {
  return __HAL_TIM_GET_COUNTER(&htim2);
}

#ifdef __cplusplus
}
#endif
