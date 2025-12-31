/**
 * @file sa818_usb_audio.h
 * @brief SA818 USB Audio Bridge API
 *
 * Integration between USB Audio Class 2 (UAC2) and SA818 audio subsystem.
 * Provides bidirectional audio streaming between USB host and SA818 radio.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#ifndef ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_USB_AUDIO_H_
#define ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_USB_AUDIO_H_

#include <sa818/sa818.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup sa818_usb_audio SA818 USB Audio Bridge
 * @ingroup sa818
 * @{
 */

/**
 * @brief Initialize USB Audio integration with SA818
 *
 * Sets up UAC2 callbacks and prepares audio buffers for streaming.
 * Must be called after USB device stack initialization.
 *
 * @param dev SA818 device instance
 * @param uac2_dev UAC2 device instance
 * @return SA818_OK on success, error code otherwise
 */
sa818_result sa818_usb_audio_init(const struct device *dev, const struct device *uac2_dev);

/**
 * @brief Enable USB audio streaming
 *
 * Starts audio data flow between USB and SA818.
 *
 * @param dev SA818 device instance
 * @return SA818_OK on success, error code otherwise
 */
sa818_result sa818_usb_audio_enable(const struct device *dev);

/**
 * @brief Disable USB audio streaming
 *
 * Stops audio data flow and releases buffers.
 *
 * @param dev SA818 device instance
 * @return SA818_OK on success, error code otherwise
 */
sa818_result sa818_usb_audio_disable(const struct device *dev);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_USB_AUDIO_H_ */
