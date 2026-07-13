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
 * @brief Register UAC2 ops and prepare the bridge context.
 *
 * MUST be called BEFORE the USB device stack is initialized (i.e. before
 * sample_usbd_init_device()/usbd_init()). The UAC2 class init hook rejects the
 * configuration with -EINVAL if the application ops are not registered yet, so
 * registering the ops late (after usbd_init) makes the whole USB device fail to
 * enumerate.
 *
 * @param uac2_dev UAC2 device instance
 * @return 0 on success, negative errno on failure
 */
int usb_audio_bridge_register_ops(const struct device *uac2_dev);

/**
 * @brief Start SA818 <-> USB audio streaming.
 *
 * Registers the SA818 audio callbacks, starts streaming, and starts the USB IN
 * thread. Call AFTER usbd_enable() and after usb_audio_bridge_register_ops().
 *
 * @param sa818_dev SA818 device instance
 * @return 0 on success, negative errno on failure
 */
int usb_audio_bridge_start(const struct device *sa818_dev);

#ifdef __cplusplus
}
#endif

#endif /* USB_AUDIO_BRIDGE_H_ */
