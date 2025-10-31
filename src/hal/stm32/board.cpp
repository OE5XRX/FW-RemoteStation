#include <tusb_config.h>

#include "../../shell/log.h"
#include "board.h"

#define SYSTEM_MEMORY_BASE 0x1FFFD800
constexpr uint32_t USB_RESET_DELAY = 100; /* ms */

static void SystemClock_Config(void) {
  HAL_StatusTypeDef status;
  (void)status;

  /* Enable external oscillator and configure PLL: 8 MHz (HSE) / 1 * 9 = 72 MHz */
  RCC_OscInitTypeDef OscConfig;
  OscConfig.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  OscConfig.HSEState       = RCC_HSE_ON;
  OscConfig.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  OscConfig.PLL.PLLState   = RCC_PLL_ON;
  OscConfig.PLL.PLLSource  = RCC_CFGR_PLLSRC_HSE_PREDIV;
  OscConfig.PLL.PLLMUL     = RCC_PLL_MUL9;

  status = HAL_RCC_OscConfig(&OscConfig);
  logger.assert(status == HAL_OK, "OscConfig");

  /* Set correct peripheral clocks. 72 MHz (PLL) / 1.5 = 48 MHz */
  RCC_PeriphCLKInitTypeDef PeriphClk;
  PeriphClk.PeriphClockSelection = RCC_PERIPHCLK_USB;
  PeriphClk.USBClockSelection    = RCC_USBCLKSOURCE_PLL_DIV1_5;

  status = HAL_RCCEx_PeriphCLKConfig(&PeriphClk);
  logger.assert(status == HAL_OK, "PeriphClk");

  /* Set up divider for maximum speeds and switch clock */
  RCC_ClkInitTypeDef ClkConfig;
  ClkConfig.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  ClkConfig.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  ClkConfig.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  ClkConfig.APB1CLKDivider = RCC_HCLK_DIV2;
  ClkConfig.APB2CLKDivider = RCC_HCLK_DIV1;

  status = HAL_RCC_ClockConfig(&ClkConfig, FLASH_LATENCY_2);
  logger.assert(status == HAL_OK, "ClkConfig");

  /* Enable Power Clock */
  __HAL_RCC_PWR_CLK_ENABLE();
}

static void USB_Reset(void) {
  /* pull USB DP pins low to simulate disconnect
     to force the host to re-enumerate when a new program is loaded */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitTypeDef GPIO_InitStruct;
  GPIO_InitStruct.Pin   = GPIO_PIN_12;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIOA->BRR = GPIO_PIN_12;
}

static void SystemReset(void) {
  uint32_t resetFlags = RCC->CSR;

  // Clear reset flags
  RCC->CSR |= RCC_CSR_RMVF;

  // Reset USB if necessary
  if (!(resetFlags & RCC_CSR_PORRSTF)) {
    // Since the USB Pullup is hardwired to the supply voltage,
    // the host (re-)enumerates our USB device only during Power-On-Reset.
    // For all other reset causes, do a manual USB reset.
    USB_Reset();

    // Use SysTick to delay before continuing
    SysTick->LOAD = ((uint32_t)USB_RESET_DELAY * (HAL_RCC_GetHCLKFreq() / 1000)) - 1;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;

    while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk))
      ;
    // Wait for timer expiration

    SysTick->CTRL = 0x00000000; // Reset SysTick
  }

  if (resetFlags & RCC_CSR_IWDGRSTF) {
    // Reset cause was watchdog, which is used for rebooting into the bootloader.
    //   Set stack pointer to *SYSTEM_MEMORY_BASE
    //   and jump to *(SYSTEM_MEMORY_BASE + 4)
    //   https://stackoverflow.com/a/42031657
    asm volatile("  msr     msp, %[sp]      \n"
                 "  bx      %[pc]           \n"

                 ::[sp] "r"(*((uint32_t *)(SYSTEM_MEMORY_BASE))),
                 [pc] "r"(*((uint32_t *)(SYSTEM_MEMORY_BASE + 4))));
  }
}

void board_init(void) {
  HAL_Init();

  __HAL_RCC_SYSCFG_CLK_ENABLE();

  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitTypeDef GpioSWOInit = {.Pin = GPIO_PIN_3, .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW, .Alternate = GPIO_AF0_TRACE};
  HAL_GPIO_Init(GPIOB, &GpioSWOInit);

  SystemReset();
  SystemClock_Config();
}

size_t board_get_unique_id(uint8_t *id) {
  volatile uint32_t *stm32_uuid = (volatile uint32_t *)UID_BASE;
  uint32_t          *id32       = (uint32_t *)(uintptr_t)id;
  uint8_t const      len        = 12;

  id32[0] = stm32_uuid[0];
  id32[1] = stm32_uuid[1];
  id32[2] = stm32_uuid[2];

  return len;
}
