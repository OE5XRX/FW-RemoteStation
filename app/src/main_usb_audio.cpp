/**
 * @file main_usb_audio.cpp
 * @brief SA818 USB Audio Application
 *
 * Demonstrates USB composite device with CDC ACM, UAC2, and DFU.
 * Shows clean separation: SA818 driver provides generic audio interface,
 * application connects it to USB Audio.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#include "usb_audio_bridge.h"

#include <sa818/sa818.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

extern "C" {
#include "sample_usbd.h"
}

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Device tree node identifiers */
#define SA818_NODE DT_ALIAS(sa818)
#define UAC2_NODE DT_NODELABEL(uac2_radio)

/* Check if USB Audio is available - warn only on actual hardware platforms */
#if !DT_NODE_EXISTS(UAC2_NODE) && !defined(CONFIG_BOARD_NATIVE_SIM)
#warning "USB Audio (uac2_radio) not found in device tree"
#endif

/**
 * @brief Main application
 */
int main(void) {
  LOG_INF("SA818 USB Audio Application Starting...");
  LOG_INF("Build: %s %s", __DATE__, __TIME__);

  int ret;

#if DT_NODE_EXISTS(UAC2_NODE)
  /* Get the UAC2 device and register its ops BEFORE the USB device stack is
   * initialized. usbd_init() calls the UAC2 class init hook, which returns
   * -EINVAL ("Application did not register UAC2 ops") if the ops are not set
   * yet -- that failure aborts the whole USB device and nothing enumerates. */
  const struct device *uac2 = DEVICE_DT_GET(UAC2_NODE);
  if (!device_is_ready(uac2)) {
    LOG_ERR("UAC2 device not ready");
    return -ENODEV;
  }
  LOG_INF("UAC2 device ready");

  ret = usb_audio_bridge_register_ops(uac2);
  if (ret != 0) {
    LOG_ERR("Failed to register UAC2 ops: %d", ret);
    return ret;
  }
#else
  LOG_WRN("USB Audio not configured in device tree");
#endif

  /* Initialize USB device (provided by common sample code) */
  struct usbd_context *sample_usbd = sample_usbd_init_device(NULL);
  if (sample_usbd == NULL) {
    LOG_ERR("Failed to initialize USB device");
    return -ENODEV;
  }

  /* Enable USB device */
  ret = usbd_enable(sample_usbd);
  if (ret != 0) {
    LOG_ERR("Failed to enable USB device: %d", ret);
    return ret;
  }
  LOG_INF("USB device enabled");

  /* Get SA818 device */
  const struct device *sa818 = DEVICE_DT_GET(SA818_NODE);
  if (!device_is_ready(sa818)) {
    LOG_ERR("SA818 device not ready");
    usbd_disable(sample_usbd);
    return -ENODEV;
  }
  LOG_INF("SA818 device ready");

#if DT_NODE_EXISTS(UAC2_NODE)
  /* Start the SA818 <-> USB audio bridge now that the USB device is enabled. */
  ret = usb_audio_bridge_start(sa818);
  if (ret != 0) {
    LOG_ERR("USB Audio Bridge start failed: %d", ret);
    usbd_disable(sample_usbd);
    return ret;
  }
  LOG_INF("USB Audio Bridge enabled");
#endif

  /* Power on SA818 */
  ret = sa818_set_power(sa818, SA818_DEVICE_ON);
  if (ret != SA818_OK) {
    LOG_ERR("Failed to power on SA818: %d", ret);
    usbd_disable(sample_usbd);
    return -EIO;
  }
  LOG_INF("SA818 powered on");

  /* Main loop - shell handles commands */
  LOG_INF("System ready. Use shell commands to control SA818.");
  LOG_INF("USB CDC ACM: Shell/Console");
  LOG_INF("USB UAC2: Audio streaming @ 8kHz");
  LOG_INF("USB DFU: Firmware update (detach to enter DFU mode)");

  while (true) {
    k_sleep(K_SECONDS(10));

    /* Optional: Print status periodically */
    sa818_status status = sa818_get_status(sa818);
    LOG_INF("SA818 Status - Power: %s, PTT: %s, SQL: %s", status.device_power == SA818_DEVICE_ON ? "ON" : "OFF",
            status.ptt_state == SA818_PTT_ON ? "ON" : "OFF", status.squelch_state == SA818_SQUELCH_OPEN ? "OPEN" : "CLOSED");
  }

  return 0;
}
