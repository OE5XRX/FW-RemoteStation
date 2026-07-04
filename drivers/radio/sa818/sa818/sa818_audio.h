/**
 * @file sa818_audio.h
 * @brief SA818 Audio Subsystem Interface
 *
 * Handles ADC/DAC configuration for audio input/output,
 * audio level monitoring, and audio path control for
 * SA818 radio module.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#ifndef ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_SA818_AUDIO_H_
#define ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_SA818_AUDIO_H_

#ifdef CONFIG_SA818

#include <sa818/sa818.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio subsystem for SA818
 *
 * Handles ADC/DAC configuration for audio input/output
 * and monitoring of audio levels.
 */

/**
 * @brief Initialize audio subsystem
 *
 * @param dev SA818 device
 * @return SA818_OK on success, error code on failure
 */
[[nodiscard]] enum sa818_result sa818_audio_init(const struct device *dev);

/**
 * @brief Set TX audio level (modulation)
 *
 * @param dev SA818 device
 * @param level Audio level (0-255)
 * @return SA818_OK on success, error code on failure
 */
[[nodiscard]] enum sa818_result sa818_audio_set_tx_level(const struct device *dev, uint8_t level);

/**
 * @brief Get RX audio level (demodulation)
 *
 * @param dev SA818 device
 * @param level Pointer to store audio level
 * @return SA818_OK on success, error code on failure
 */
[[nodiscard]] enum sa818_result sa818_audio_get_rx_level(const struct device *dev, uint16_t *level);

/**
 * @brief Enable/disable audio paths
 *
 * @param dev SA818 device
 * @param rx_enable Enable RX audio path
 * @param tx_enable Enable TX audio path
 * @return SA818_OK on success, error code on failure
 */
[[nodiscard]] enum sa818_result sa818_audio_enable_path(const struct device *dev, bool rx_enable, bool tx_enable);

/**
 * @brief Generate test tone on TX audio output
 *
 * If a test tone is already active, it will be stopped and replaced with the new tone.
 *
 * @param dev SA818 device
 * @param freq_hz Tone frequency in Hz (100-3000)
 * @param duration_ms Duration in milliseconds (0 = continuous, max 3600000 for 1 hour)
 * @param amplitude Tone amplitude (0-255)
 * @return SA818_OK on success; SA818_ERROR_INVALID_DEVICE if @p dev is not a valid SA818 device;
 *         SA818_ERROR_INVALID_PARAM if @p freq_hz or @p duration_ms is out of range;
 *         SA818_ERROR_DAC if the DAC device is not available
 */
[[nodiscard]] enum sa818_result sa818_audio_generate_test_tone(const struct device *dev, uint16_t freq_hz, uint32_t duration_ms, uint8_t amplitude);

/**
 * @brief Stop test tone generation
 *
 * @param dev SA818 device
 * @return SA818_OK on success, error code on failure
 */
[[nodiscard]] enum sa818_result sa818_audio_stop_test_tone(const struct device *dev);

/**
 * @brief Generate a continuous frequency sweep on the TX audio output
 *
 * Linearly sweeps the tone frequency from @p start_freq_hz to @p end_freq_hz
 * over @p duration_ms, then loops back to the start and repeats endlessly.
 * If a test tone or sweep is already active it is stopped and replaced.
 * Stop the sweep with sa818_audio_stop_test_tone().
 *
 * @param dev SA818 device
 * @param start_freq_hz Start frequency in Hz (100-3000)
 * @param end_freq_hz End frequency in Hz (100-3000, must be > start_freq_hz)
 * @param duration_ms Sweep cycle duration in milliseconds (1000-60000)
 * @param amplitude Sweep amplitude (0-255)
 * @return SA818_OK on success; SA818_ERROR_INVALID_DEVICE if @p dev is not a valid SA818 device;
 *         SA818_ERROR_INVALID_PARAM if any parameter is out of range;
 *         SA818_ERROR_DAC if the DAC device is not available
 */
[[nodiscard]] enum sa818_result sa818_audio_generate_sweep(const struct device *dev, uint16_t start_freq_hz, uint16_t end_freq_hz, uint32_t duration_ms,
                                                           uint8_t amplitude);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_SA818 */

#endif /* ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_SA818_AUDIO_H_ */
