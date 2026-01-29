/**
 * @file usb_config.cpp
 * @brief Configure USB Device
 *
 * @copyright Copyright (c) 2026 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/class/usbd_uac2.h>
#include <zephyr/usb/usbd.h>

#define CONFIG_CDC_ACM_SERIAL_MANUFACTURER_STRING "OE5XRX"
#define CONFIG_CDC_ACM_SERIAL_PRODUCT_STRING "FM Transceiver Board"
#define CONFIG_CDC_ACM_SERIAL_VID 0x2FE3
#define CONFIG_CDC_ACM_SERIAL_PID 0x0012
#define CONFIG_CDC_ACM_SERIAL_MAX_POWER 250

LOG_MODULE_REGISTER(usb_config, CONFIG_USBD_LOG_LEVEL);

USBD_DEVICE_DEFINE(cdc_acm_serial, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), CONFIG_CDC_ACM_SERIAL_VID, CONFIG_CDC_ACM_SERIAL_PID);

USBD_DESC_LANG_DEFINE(cdc_acm_serial_lang);
USBD_DESC_MANUFACTURER_DEFINE(cdc_acm_serial_mfr, CONFIG_CDC_ACM_SERIAL_MANUFACTURER_STRING);
USBD_DESC_PRODUCT_DEFINE(cdc_acm_serial_product, CONFIG_CDC_ACM_SERIAL_PRODUCT_STRING);
IF_ENABLED(CONFIG_HWINFO, (USBD_DESC_SERIAL_NUMBER_DEFINE(cdc_acm_serial_sn)));

USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");

static const uint8_t attributes = 0;

USBD_CONFIGURATION_DEFINE(cdc_acm_serial_fs_config, attributes, CONFIG_CDC_ACM_SERIAL_MAX_POWER, &fs_cfg_desc);

static int register_cdc_acm_0(struct usbd_context *const uds_ctx, const enum usbd_speed speed) {
  struct usbd_config_node *cfg_nd;
  int err;

  cfg_nd = &cdc_acm_serial_fs_config;

  err = usbd_add_configuration(uds_ctx, speed, cfg_nd);
  if (err) {
    LOG_ERR("Failed to add configuration");
    return err;
  }

  /* Register CDC ACM class */
  err = usbd_register_class(&cdc_acm_serial, "cdc_acm_0", speed, 1);
  if (err) {
    LOG_ERR("Failed to register CDC ACM class");
    return err;
  }

  /* Register UAC2 class for audio streaming */
  err = usbd_register_class(&cdc_acm_serial, "uac2_radio", speed, 2);
  if (err) {
    LOG_ERR("Failed to register UAC2 class");
    return err;
  }

  return usbd_device_set_code_triple(uds_ctx, speed, USB_BCC_MISCELLANEOUS, 0x02, 0x01);
}

/* USB Pullup: PB12 - just for V0.2 board revision, remove for next revision */
static const struct gpio_dt_spec pullup = {.port = DEVICE_DT_GET(DT_NODELABEL(gpiob)), .pin = 12, .dt_flags = GPIO_ACTIVE_HIGH};

static int cdc_acm_serial_init_device(void) {
  int err;

  err = usbd_add_descriptor(&cdc_acm_serial, &cdc_acm_serial_lang);
  if (err) {
    LOG_ERR("Failed to initialize %s (%d)", "language descriptor", err);
    return err;
  }

  err = usbd_add_descriptor(&cdc_acm_serial, &cdc_acm_serial_mfr);
  if (err) {
    LOG_ERR("Failed to initialize %s (%d)", "manufacturer descriptor", err);
    return err;
  }

  err = usbd_add_descriptor(&cdc_acm_serial, &cdc_acm_serial_product);
  if (err) {
    LOG_ERR("Failed to initialize %s (%d)", "product descriptor", err);
    return err;
  }

  IF_ENABLED(CONFIG_HWINFO, (err = usbd_add_descriptor(&cdc_acm_serial, &cdc_acm_serial_sn);))
  if (err) {
    LOG_ERR("Failed to initialize %s (%d)", "SN descriptor", err);
    return err;
  }

  err = register_cdc_acm_0(&cdc_acm_serial, USBD_SPEED_FS);
  if (err) {
    return err;
  }

  err = usbd_init(&cdc_acm_serial);
  if (err) {
    LOG_ERR("Failed to initialize %s (%d)", "device support", err);
    return err;
  }

  /* Sicher disconnected - just for V0.2 board revision, remove for next revision */
  gpio_pin_configure_dt(&pullup, GPIO_INPUT);
  k_sleep(K_MSEC(300)); /* Host muss Disconnect sehen */
  /* end remove */

  err = usbd_enable(&cdc_acm_serial);
  if (err) {
    LOG_ERR("Failed to enable %s (%d)", "device support", err);
    return err;
  }

  /* Pull-Up aktivieren - just for V0.2 board revision, remove for next revision */
  k_sleep(K_MSEC(50));
  gpio_pin_configure_dt(&pullup, GPIO_OUTPUT_HIGH);
  /* end remove */

  return 0;
}

SYS_INIT(cdc_acm_serial_init_device, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
