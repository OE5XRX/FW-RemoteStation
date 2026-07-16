/**
 * @file audio_stream.h
 * @brief Generic hardware-timed audio streaming bridge
 *
 * Callback-based bidirectional audio streaming that connects an application
 * audio source/sink (USB, I2S, file, network, ...) to the hardware-timed
 * capture/playback backend. The sample timing is delegated to that backend
 * (currently the analog-audio-in / analog-audio-out TIM+ADC/DAC+DMA modules);
 * this layer only bridges PCM to/from the application callbacks.
 *
 * This module is intentionally radio-agnostic: it takes an opaque @p dev handle
 * that it passes straight back to the callbacks as context, and depends on no
 * particular transceiver driver. Swapping the audio backend (e.g. to an I2S
 * codec) is a change here, not in the radio driver.
 *
 * @copyright Copyright (c) 2026 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#ifndef OE5XRX_APP_AUDIO_STREAM_H_
#define OE5XRX_APP_AUDIO_STREAM_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Audio sample format. */
struct audio_format {
  uint32_t sample_rate; /**< Sample rate in Hz (typically 8000) */
  uint8_t bit_depth;    /**< Bits per sample (typically 16) */
  uint8_t channels;     /**< Number of channels (1=mono, 2=stereo) */
};

/**
 * @brief TX audio request callback
 *
 * Invoked when the playback backend needs PCM samples. Fill @p buffer and
 * return the number of bytes written (0 when no data is available).
 *
 * @param dev       Opaque context handle from audio_stream_register()
 * @param buffer    Buffer to fill with PCM audio
 * @param size      Number of bytes requested
 * @param user_data User data pointer from callback registration
 * @return Bytes written to @p buffer
 */
typedef size_t (*audio_stream_tx_request_cb)(const struct device *dev, uint8_t *buffer, size_t size, void *user_data);

/**
 * @brief RX audio data callback
 *
 * Invoked when the capture backend has PCM samples available.
 *
 * @param dev       Opaque context handle from audio_stream_register()
 * @param buffer    Buffer containing PCM audio
 * @param size      Number of bytes in @p buffer
 * @param user_data User data pointer from callback registration
 */
typedef void (*audio_stream_rx_data_cb)(const struct device *dev, const uint8_t *buffer, size_t size, void *user_data);

/** Audio streaming callbacks. */
struct audio_stream_callbacks {
  audio_stream_tx_request_cb tx_request; /**< TX audio request (playback) */
  audio_stream_rx_data_cb rx_data;       /**< RX audio available (capture) */
  void *user_data;                       /**< User data for the callbacks */
};

/**
 * @brief Register audio streaming callbacks.
 * @param dev       Opaque context handle passed back to the callbacks.
 * @param callbacks Callback structure.
 * @return 0 on success, negative errno otherwise.
 */
int audio_stream_register(const struct device *dev, const struct audio_stream_callbacks *callbacks);

/**
 * @brief Start audio streaming (starts the capture/playback backend).
 * @return 0 on success, negative errno otherwise.
 */
int audio_stream_start(const struct device *dev, const struct audio_format *format);

/**
 * @brief Stop audio streaming.
 * @return 0 on success, negative errno otherwise.
 */
int audio_stream_stop(const struct device *dev);

/**
 * @brief Get the current audio format.
 * @return 0 on success, negative errno otherwise.
 */
int audio_stream_get_format(const struct device *dev, struct audio_format *format);

#ifdef __cplusplus
}
#endif

#endif /* OE5XRX_APP_AUDIO_STREAM_H_ */
