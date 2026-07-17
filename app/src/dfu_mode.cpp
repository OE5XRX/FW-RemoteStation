/**
 * @file dfu_mode.cpp
 * @brief DFU-only mode context: composite→DFU switch and upgrade request.
 *
 * Mirrors the switch_to_dfu_mode() sequence from
 * zephyr/samples/subsys/usb/dfu/src/main.c, adapted for C++20 and the OE5XRX
 * project conventions (no dynamic allocation, no exceptions, no RTTI).
 *
 * Compiled only when CONFIG_BOOTLOADER_MCUBOOT is set (prod/sysbuild build).
 *
 * @copyright Copyright (c) 2026 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#ifdef CONFIG_BOOTLOADER_MCUBOOT

#include "dfu_mode.h"

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/usb/usbd.h>

extern "C" {
#include "sample_usbd.h"
}

LOG_MODULE_REGISTER(dfu_mode, LOG_LEVEL_INF);

/* ---------------------------------------------------------------------------
 * Static DFU-only USBD context.
 *
 * A separate USBD_DEVICE_DEFINE is required: the composite context is shut
 * down before DFU mode is entered, so we cannot reuse it.  The same UDC
 * controller node (zephyr_udc0) is shared — that is intentional; only one
 * context is active at a time.
 *
 * VID/PID reuse the same CONFIG_SAMPLE_USBD_VID / CONFIG_SAMPLE_USBD_PID
 * values as the composite so dfu-util can identify the device consistently.
 * --------------------------------------------------------------------------*/
USBD_DEVICE_DEFINE(dfu_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), CONFIG_SAMPLE_USBD_VID, CONFIG_SAMPLE_USBD_PID);

/* Language descriptor (mandatory, must be added before configurations). */
USBD_DESC_LANG_DEFINE(dfu_lang);

/* Configuration descriptors for FS (and optionally HS). */
USBD_DESC_CONFIG_DEFINE(dfu_fs_cfg_desc, "DFU FS Configuration");
USBD_DESC_CONFIG_DEFINE(dfu_hs_cfg_desc, "DFU HS Configuration");

static const uint8_t dfu_cfg_attributes =
    (IS_ENABLED(CONFIG_SAMPLE_USBD_SELF_POWERED) ? USB_SCD_SELF_POWERED : 0) | (IS_ENABLED(CONFIG_SAMPLE_USBD_REMOTE_WAKEUP) ? USB_SCD_REMOTE_WAKEUP : 0);

USBD_CONFIGURATION_DEFINE(dfu_fs_config, dfu_cfg_attributes, CONFIG_SAMPLE_USBD_MAX_POWER, &dfu_fs_cfg_desc);

USBD_CONFIGURATION_DEFINE(dfu_hs_config, dfu_cfg_attributes, CONFIG_SAMPLE_USBD_MAX_POWER, &dfu_hs_cfg_desc);

/* ---------------------------------------------------------------------------
 * DFU-only message callback
 * --------------------------------------------------------------------------*/
static void dfu_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *const msg) {
  ARG_UNUSED(ctx);
  LOG_INF("DFU mode USBD message: %s", usbd_msg_type_string(msg->type));

  if (msg->type == USBD_MSG_DFU_DOWNLOAD_COMPLETED) {
    dfu_mode_download_completed();
  }
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------*/

void dfu_mode_switch_to_dfu(struct usbd_context *composite) {
  int err;

  LOG_INF("DFU detach: tearing down composite USB device");
  usbd_disable(composite);
  usbd_shutdown(composite);

  /* Build the DFU-only context.
   * Follow the sample sequence exactly:
   *   1. add language descriptor
   *   2. add HS config + register dfu_dfu class (if HS capable)
   *   3. add FS config + register dfu_dfu class
   *   4. set code triple (DFU: 0,0,0)
   *   5. init
   *   6. register msg callback
   *   7. enable
   */

  err = usbd_add_descriptor(&dfu_usbd, &dfu_lang);
  if (err) {
    LOG_ERR("DFU: failed to add language descriptor (%d) — rebooting", err);
    sys_reboot(SYS_REBOOT_COLD);
  }

  if (usbd_caps_speed(&dfu_usbd) == USBD_SPEED_HS) {
    err = usbd_add_configuration(&dfu_usbd, USBD_SPEED_HS, &dfu_hs_config);
    if (err) {
      LOG_ERR("DFU: failed to add HS configuration (%d) — rebooting", err);
      sys_reboot(SYS_REBOOT_COLD);
    }

    err = usbd_register_class(&dfu_usbd, "dfu_dfu", USBD_SPEED_HS, 1);
    if (err) {
      LOG_ERR("DFU: failed to register dfu_dfu class (HS) (%d) — rebooting", err);
      sys_reboot(SYS_REBOOT_COLD);
    }

    usbd_device_set_code_triple(&dfu_usbd, USBD_SPEED_HS, 0, 0, 0);
  }

  err = usbd_add_configuration(&dfu_usbd, USBD_SPEED_FS, &dfu_fs_config);
  if (err) {
    LOG_ERR("DFU: failed to add FS configuration (%d) — rebooting", err);
    sys_reboot(SYS_REBOOT_COLD);
  }

  err = usbd_register_class(&dfu_usbd, "dfu_dfu", USBD_SPEED_FS, 1);
  if (err) {
    LOG_ERR("DFU: failed to register dfu_dfu class (FS) (%d) — rebooting", err);
    sys_reboot(SYS_REBOOT_COLD);
  }

  usbd_device_set_code_triple(&dfu_usbd, USBD_SPEED_FS, 0, 0, 0);

  err = usbd_init(&dfu_usbd);
  if (err) {
    LOG_ERR("DFU: failed to initialize DFU-only USB device (%d) — rebooting", err);
    sys_reboot(SYS_REBOOT_COLD);
  }

  err = usbd_msg_register_cb(&dfu_usbd, dfu_msg_cb);
  if (err) {
    LOG_ERR("DFU: failed to register DFU message callback (%d) — rebooting", err);
    sys_reboot(SYS_REBOOT_COLD);
  }

  err = usbd_enable(&dfu_usbd);
  if (err) {
    LOG_ERR("DFU: failed to enable DFU-only USB device (%d) — rebooting", err);
    sys_reboot(SYS_REBOOT_COLD);
  }

  LOG_INF("DFU mode active — waiting for download");
}

void dfu_mode_download_completed(void) {
  LOG_INF("DFU download completed — requesting upgrade (test mode)");
  int err = boot_request_upgrade(false);
  if (err != 0) {
    LOG_ERR("boot_request_upgrade failed: %d", err);
  }
}

#endif /* CONFIG_BOOTLOADER_MCUBOOT */
