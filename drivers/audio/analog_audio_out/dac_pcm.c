/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "dac_pcm.h"

uint16_t pcm16_to_dac(int16_t sample, uint8_t resolution) {
  if (resolution < 1) {
    resolution = 1;
  } else if (resolution > 16) {
    resolution = 16;
  }
  uint32_t unsigned_sample = (uint32_t)((int32_t)sample + 32768);
  uint8_t down_shift = (uint8_t)(16 - resolution);
  return (uint16_t)(unsigned_sample >> down_shift);
}
