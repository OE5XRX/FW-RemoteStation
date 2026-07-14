/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef OE5XRX_AUDIO_ANALOG_AUDIO_OUT_H_
#define OE5XRX_AUDIO_ANALOG_AUDIO_OUT_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Fill up to @p max PCM samples into @p dst; return the count provided (0..max).
 *  Runs in thread context (system workqueue); may take a mutex. */
typedef size_t (*analog_audio_out_src)(int16_t *dst, size_t max, void *user_data);

/** Start hardware-timed playback; @p src is polled to refill each DMA block. */
int analog_audio_out_start(const struct device *dev, analog_audio_out_src src, void *user_data);

/** Stop playback. */
int analog_audio_out_stop(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* OE5XRX_AUDIO_ANALOG_AUDIO_OUT_H_ */
