/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "adc_pcm.h"

int16_t adc_to_pcm16(uint16_t raw, uint8_t resolution) {
  if (resolution < 1) {
    resolution = 1;
  } else if (resolution > 16) {
    resolution = 16;
  }
  uint8_t up_shift = (uint8_t)(16 - resolution);
  int32_t full = ((int32_t)raw << up_shift) - 32768;
  return (int16_t)full;
}
