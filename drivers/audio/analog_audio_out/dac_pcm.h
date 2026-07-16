/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef OE5XRX_ANALOG_AUDIO_OUT_DAC_PCM_H_
#define OE5XRX_ANALOG_AUDIO_OUT_DAC_PCM_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Convert signed 16-bit PCM to an unsigned DAC code of @p resolution bits.
 * Offsets by the midpoint so PCM 0 maps to DAC mid-scale (silence), then scales
 * down to the DAC resolution. @p resolution is clamped to [1, 16].
 */
uint16_t pcm16_to_dac(int16_t sample, uint8_t resolution);

#ifdef __cplusplus
}
#endif

#endif /* OE5XRX_ANALOG_AUDIO_OUT_DAC_PCM_H_ */
