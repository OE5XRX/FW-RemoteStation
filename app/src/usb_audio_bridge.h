/**
 * @file usb_audio_bridge.h
 * @brief USB Audio Bridge for SA818
 *
 * Application-level USB Audio Class 2 integration.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#ifndef USB_AUDIO_BRIDGE_H_
#define USB_AUDIO_BRIDGE_H_

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize USB Audio Bridge
 *
 * Connects SA818 audio streaming with USB Audio Class 2.
 * Must be called after USB device stack initialization.
 *
 * @param sa818_dev SA818 device instance
 * @param uac2_dev UAC2 device instance
 * @return 0 on success, negative errno on failure
 */
int usb_audio_bridge_init(const struct device *sa818_dev,
                           const struct device *uac2_dev);

#ifdef __cplusplus
}
#endif

#endif /* USB_AUDIO_BRIDGE_H_ */
