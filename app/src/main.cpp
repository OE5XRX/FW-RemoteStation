/**
 * @file main.cpp
 * @brief FM Transceiver Main Application
 *
 * @copyright Copyright (c) 2026 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#include "audio_bridge/bridge.h"

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void) {}

static int init_audio_bridge(void) {
  LOG_INF("FM Transceiver Application Starting");

  /* Get device references from device tree */
  const struct device *sa818_dev = DEVICE_DT_GET(DT_ALIAS(sa818));
  if (!device_is_ready(sa818_dev)) {
    LOG_ERR("SA818 device not ready");
    return -ENODEV;
  }
  LOG_INF("SA818 device ready");

  const struct device *uac2_dev = DEVICE_DT_GET(DT_NODELABEL(uac2_radio));
  if (!device_is_ready(uac2_dev)) {
    LOG_ERR("UAC2 device not ready");
    return -ENODEV;
  }
  LOG_INF("UAC2 device ready");

  /* Initialize USB Audio Bridge to connect USB UAC2 with SA818 */
  int ret = oe5xrx::audio::UsbAudioBridge::instance().initialize(sa818_dev, uac2_dev);
  if (ret != 0) {
    LOG_ERR("Failed to initialize USB Audio Bridge: %d", ret);
    return ret;
  }

  LOG_INF("System initialization complete");
  LOG_INF("USB Audio <-> SA818 Radio bridge active");

  return 0;
}

SYS_INIT(init_audio_bridge, APPLICATION, 80);
