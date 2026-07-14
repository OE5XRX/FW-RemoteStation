/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef OE5XRX_AUDIO_ANALOG_AUDIO_IN_H_
#define OE5XRX_AUDIO_ANALOG_AUDIO_IN_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes must be checked (driver-layer convention, CLAUDE.md). Use the
 * GCC attribute so the warning applies to both C and C++ consumers. */
#define ANALOG_AUDIO_IN_MUST_CHECK __attribute__((warn_unused_result))

/**
 * Delivers a batch of converted 16-bit PCM samples. Invoked from the system
 * workqueue thread (not IRQ context), so the consumer may block / take a mutex.
 */
typedef void (*analog_audio_in_cb)(const int16_t *samples, size_t count, void *user_data);

/** Start hardware-timed capture; @p cb is invoked once per DMA half/full block. */
ANALOG_AUDIO_IN_MUST_CHECK int analog_audio_in_start(const struct device *dev, analog_audio_in_cb cb, void *user_data);

/** Stop capture. */
ANALOG_AUDIO_IN_MUST_CHECK int analog_audio_in_stop(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* OE5XRX_AUDIO_ANALOG_AUDIO_IN_H_ */
