/**
 * @file sa818_audio_stream.h
 * @brief SA818 Audio Streaming API
 *
 * Generic audio streaming interface for SA818 audio subsystem.
 * Provides callback-based bidirectional audio streaming that can be
 * connected to various audio sources/sinks (USB, I2S, file, network, etc.)
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#ifndef ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_AUDIO_STREAM_H_
#define ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_AUDIO_STREAM_H_

#include <sa818/sa818.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup sa818_audio_stream SA818 Audio Streaming
 * @ingroup sa818
 * @{
 */

/**
 * @brief Audio sample format for SA818
 */
struct sa818_audio_format {
  uint32_t sample_rate; /**< Sample rate in Hz (typically 8000) */
  uint8_t bit_depth;    /**< Bits per sample (typically 16) */
  uint8_t channels;     /**< Number of channels (1=mono, 2=stereo) */
};

/**
 * @brief TX audio request callback
 *
 * Called by SA818 driver when it needs audio samples for transmission.
 * Application should fill the buffer with PCM audio data.
 *
 * @param dev SA818 device
 * @param buffer Buffer to fill with audio samples
 * @param size Number of bytes requested
 * @param user_data User data pointer from callback registration
 * @return Number of bytes written to buffer (0 if no data available)
 */
typedef size_t (*sa818_audio_tx_request_cb)(const struct device *dev, uint8_t *buffer, size_t size, void *user_data);

/**
 * @brief RX audio data callback
 *
 * Called by SA818 driver when received audio samples are available.
 * Application should process/store the PCM audio data.
 *
 * @param dev SA818 device
 * @param buffer Buffer containing audio samples
 * @param size Number of bytes in buffer
 * @param user_data User data pointer from callback registration
 */
typedef void (*sa818_audio_rx_data_cb)(const struct device *dev, const uint8_t *buffer, size_t size, void *user_data);

/**
 * @brief Audio streaming callbacks
 */
struct sa818_audio_callbacks {
  sa818_audio_tx_request_cb tx_request; /**< TX audio request */
  sa818_audio_rx_data_cb rx_data;       /**< RX audio data available */
  void *user_data;                      /**< User data for callbacks */
};

/**
 * @brief Register audio streaming callbacks
 *
 * Sets up callbacks for bidirectional audio streaming.
 *
 * @param dev SA818 device instance
 * @param callbacks Callback structure
 * @return SA818_OK on success, error code otherwise
 */
sa818_result sa818_audio_stream_register(const struct device *dev, const struct sa818_audio_callbacks *callbacks);

/**
 * @brief Start audio streaming
 *
 * Begins calling registered callbacks for audio data.
 *
 * @param dev SA818 device instance
 * @param format Audio format configuration
 * @return SA818_OK on success, error code otherwise
 */
sa818_result sa818_audio_stream_start(const struct device *dev, const struct sa818_audio_format *format);

/**
 * @brief Stop audio streaming
 *
 * Stops calling callbacks.
 *
 * @param dev SA818 device instance
 * @return SA818_OK on success, error code otherwise
 */
sa818_result sa818_audio_stream_stop(const struct device *dev);

/**
 * @brief Get current audio format
 *
 * @param dev SA818 device instance
 * @param format Output format structure
 * @return SA818_OK on success, error code otherwise
 */
sa818_result sa818_audio_stream_get_format(const struct device *dev, struct sa818_audio_format *format);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_AUDIO_STREAM_H_ */
