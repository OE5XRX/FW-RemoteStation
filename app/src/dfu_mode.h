/**
 * @file dfu_mode.h
 * @brief DFU-only mode switch support for MCUboot-enabled prod builds.
 *
 * When the composite USB device receives DFU_DETACH (USBD_MSG_DFU_APP_DETACH),
 * the composite context must be torn down and replaced with a DFU-only context
 * so the host can download a new image to slot1.
 *
 * This module is compiled only when CONFIG_BOOTLOADER_MCUBOOT is set (i.e. the
 * sysbuild / prod variant).  The bare build never includes it.
 *
 * @copyright Copyright (c) 2026 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#pragma once

#ifdef CONFIG_BOOTLOADER_MCUBOOT

#include <zephyr/usb/usbd.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Switch from composite runtime mode to DFU-only mode.
 *
 * Must be called from the USBD message callback on USBD_MSG_DFU_APP_DETACH.
 * Tears down @p composite, builds a DFU-only USBD context, and enables it.
 * The DFU-only context registers its own message callback that handles
 * USBD_MSG_DFU_DOWNLOAD_COMPLETED by calling boot_request_upgrade(false).
 *
 * @param composite  The currently active composite usbd_context.
 */
void dfu_mode_switch_to_dfu(struct usbd_context *composite);

/**
 * @brief Request an upgrade on next boot (called after DFU download completes).
 *
 * Sets the MCUboot upgrade flag for a test-mode swap.  The health gate
 * confirms the new image only after all health criteria pass; if they do not,
 * the IWDG fires and MCUboot reverts automatically.
 */
void dfu_mode_download_completed(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_BOOTLOADER_MCUBOOT */
