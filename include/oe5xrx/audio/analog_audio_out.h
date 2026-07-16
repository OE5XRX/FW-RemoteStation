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

/* Result codes must be checked (driver-layer convention, CLAUDE.md). Use the
 * GCC/Clang attribute (both define __GNUC__) so the warning applies to C and
 * C++ consumers; degrade to nothing on other toolchains. */
#if defined(__GNUC__)
#define ANALOG_AUDIO_OUT_MUST_CHECK __attribute__((warn_unused_result))
#else
#define ANALOG_AUDIO_OUT_MUST_CHECK
#endif

/** Fill up to @p max PCM samples into @p dst; return the count provided (0..max).
 *  Runs in thread context (system workqueue); may take a mutex. */
typedef size_t (*analog_audio_out_src)(int16_t *dst, size_t max, void *user_data);

/** Start hardware-timed playback; @p src is polled to refill each DMA block. */
ANALOG_AUDIO_OUT_MUST_CHECK int analog_audio_out_start(const struct device *dev, analog_audio_out_src src, void *user_data);

/** Stop playback. */
ANALOG_AUDIO_OUT_MUST_CHECK int analog_audio_out_stop(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* OE5XRX_AUDIO_ANALOG_AUDIO_OUT_H_ */
