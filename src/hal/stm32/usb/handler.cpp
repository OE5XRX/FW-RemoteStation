#include <stm32f3xx_hal.h>

#include <tusb.h>

#ifdef __cplusplus
extern "C" {
#endif
// USB defaults to using interrupts 19, 20 and 42, however, this BSP sets the
// SYSCFG_CFGR1.USB_IT_RMP bit remapping interrupts to 74, 75 and 76.

// FIXME: Do all three need to be handled, or just the LP one?
// USB high-priority interrupt (Channel 74): Triggered only by a correct
// transfer event for isochronous and double-buffer bulk transfer to reach
// the highest possible transfer rate.
void USB_HP_IRQHandler(void) {
  tud_int_handler(0);
}

// USB low-priority interrupt (Channel 75): Triggered by all USB events
// (Correct transfer, USB reset, etc.). The firmware has to check the
// interrupt source before serving the interrupt.
void USB_LP_IRQHandler(void) {
  tud_int_handler(0);
}

// USB wakeup interrupt (Channel 76): Triggered by the wakeup event from the USB
// Suspend mode.
void USBWakeUp_RMP_IRQHandler(void) {
  tud_int_handler(0);
}

void HardFault_Handler(void) {
  asm("bkpt 0");
}

#ifdef __cplusplus
}
#endif
