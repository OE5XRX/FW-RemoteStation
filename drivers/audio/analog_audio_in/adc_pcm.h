/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef OE5XRX_ANALOG_AUDIO_IN_ADC_PCM_H_
#define OE5XRX_ANALOG_AUDIO_IN_ADC_PCM_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Convert one unsigned ADC reading to signed 16-bit PCM.
 *
 * The reading is left-justified from @p resolution bits up to the full 16-bit
 * range, then offset by the midpoint so silence (mid-scale) maps to 0.
 * @p resolution is clamped to [1, 16].
 */
int16_t adc_to_pcm16(uint16_t raw, uint8_t resolution);

#ifdef __cplusplus
}
#endif

#endif /* OE5XRX_ANALOG_AUDIO_IN_ADC_PCM_H_ */
