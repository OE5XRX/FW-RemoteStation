/**
 * @file main_usb_audio.cpp
 * @brief SA818 USB Audio Application Example
 *
 * Demonstrates USB composite device with CDC ACM, UAC2, and DFU.
 * This file shows how to integrate the SA818 driver with USB Audio.
 *
 * To build for FM Board with USB Audio:
 *   west build -p -b fm_board/stm32f302xc app -- \
 *     -DEXTRA_DTC_OVERLAY_FILE="boards/oe5xrx/fm_board/fm_board_usb_composite.overlay"
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#include <sa818/sa818.h>
#include <sa818/sa818_usb_audio.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/class/usbd_uac2.h>
#include <zephyr/usb/usbd.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Device tree node identifiers */
#define SA818_NODE DT_ALIAS(sa818)
#define UAC2_NODE DT_NODELABEL(uac2_radio)

/* Check if USB Audio is available */
#if !DT_NODE_EXISTS(UAC2_NODE)
#warning "USB Audio (uac2_radio) not found in device tree"
#endif

/**
 * @brief USB device initialization
 */
static int usb_init(void) {
  const struct device *usbd = DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0));

  if (!device_is_ready(usbd)) {
    LOG_ERR("USB device not ready");
    return -ENODEV;
  }

  /* Initialize USB device stack */
  int ret = usbd_init(usbd);
  if (ret != 0) {
    LOG_ERR("USB init failed: %d", ret);
    return ret;
  }

  /* Enable USB device */
  ret = usbd_enable(usbd);
  if (ret != 0) {
    LOG_ERR("USB enable failed: %d", ret);
    return ret;
  }

  LOG_INF("USB device initialized");
  return 0;
}

/**
 * @brief Main application
 */
int main(void) {
  LOG_INF("SA818 USB Audio Application Starting...");
  LOG_INF("Build: %s %s", __DATE__, __TIME__);

  /* Get SA818 device */
  const struct device *sa818 = DEVICE_DT_GET(SA818_NODE);
  if (!device_is_ready(sa818)) {
    LOG_ERR("SA818 device not ready");
    return -ENODEV;
  }
  LOG_INF("SA818 device ready");

  /* Initialize USB device stack */
  int ret = usb_init();
  if (ret != 0) {
    LOG_ERR("USB initialization failed: %d", ret);
    return ret;
  }

#if DT_NODE_EXISTS(UAC2_NODE)
  /* Get UAC2 device */
  const struct device *uac2 = DEVICE_DT_GET(UAC2_NODE);
  if (!device_is_ready(uac2)) {
    LOG_ERR("UAC2 device not ready");
    return -ENODEV;
  }
  LOG_INF("UAC2 device ready");

  /* Initialize USB Audio integration */
  ret = sa818_usb_audio_init(sa818, uac2);
  if (ret != SA818_OK) {
    LOG_ERR("USB Audio init failed: %d", ret);
    return -EIO;
  }

  /* Enable USB Audio */
  ret = sa818_usb_audio_enable(sa818);
  if (ret != SA818_OK) {
    LOG_ERR("USB Audio enable failed: %d", ret);
    return -EIO;
  }

  LOG_INF("USB Audio enabled and ready");
#else
  LOG_WRN("USB Audio not configured in device tree");
#endif

  /* Power on SA818 */
  ret = sa818_set_power(sa818, SA818_DEVICE_ON);
  if (ret != SA818_OK) {
    LOG_ERR("Failed to power on SA818: %d", ret);
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
    LOG_INF("SA818 Status - Power: %s, PTT: %s, SQL: %s",
            status.device_power == SA818_DEVICE_ON ? "ON" : "OFF",
            status.ptt_state == SA818_PTT_ON ? "ON" : "OFF",
            status.squelch_state == SA818_SQUELCH_OPEN ? "OPEN" : "CLOSED");
  }

  return 0;
}
