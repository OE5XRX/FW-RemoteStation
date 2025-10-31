
#include <stm32f3xx_hal.h>
#include <tusb_config.h>

#include <tusb.h>

#include "dfu.h"

void tud_dfu_runtime_reboot_to_dfu_cb(void) {

  /* Use the watchdog timer for a timed reset. This assures
   * the host can close the open pipe before the device reboots. */

  IWDG_HandleTypeDef IWDGHandle = {.Instance = IWDG, .Init = {.Prescaler = IWDG_PRESCALER_4, .Reload = 128, .Window = 0x0FFF}};

  HAL_IWDG_Init(&IWDGHandle);
}
