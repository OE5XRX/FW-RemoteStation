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
 * @return 0 on success, negative errno on failure
 */
int sa818_audio_init(const struct device *dev);

/**
 * @brief Set TX audio level (modulation)
 *
 * @param dev SA818 device
 * @param level Audio level (0-255)
 * @return 0 on success, negative errno on failure
 */
int sa818_audio_set_tx_level(const struct device *dev, uint8_t level);

/**
 * @brief Get RX audio level (demodulation)
 *
 * @param dev SA818 device
 * @param level Pointer to store audio level
 * @return 0 on success, negative errno on failure
 */
int sa818_audio_get_rx_level(const struct device *dev, uint16_t *level);

/**
 * @brief Enable/disable audio paths
 *
 * @param dev SA818 device
 * @param rx_enable Enable RX audio path
 * @param tx_enable Enable TX audio path
 * @return 0 on success, negative errno on failure
 */
int sa818_audio_enable_path(const struct device *dev, bool rx_enable, bool tx_enable);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_SA818_AUDIO_H_ */
