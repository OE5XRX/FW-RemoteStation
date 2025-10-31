#ifndef BOARD_H_
#define BOARD_H_

#include <stm32f3xx_hal.h>

#include <tusb.h>

void   board_init(void);
size_t board_get_unique_id(uint8_t *id);

#endif
