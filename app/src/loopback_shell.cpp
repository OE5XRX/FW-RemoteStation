/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * `loopback` shell command — test-only UAC2 internal loopback control.
 *
 * Arms/disarms the bench test mode that routes USB OUT (host -> device) audio
 * straight back to USB IN (device -> host), bypassing the SA818. Lets the
 * host-side audio path be exercised without a working radio link. Compiled only
 * when CONFIG_FM_TEST_LOOPBACK is set (default n); never present in a production
 * build.
 */
#include "usb_audio_bridge.h"

#include <zephyr/shell/shell.h>

static int cmd_loopback_on(const struct shell *sh, size_t argc, char **argv) {
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  usb_audio_bridge_set_loopback(true);
  shell_print(sh, "loopback enabled (USB OUT -> USB IN, SA818 bypassed)");
  return 0;
}

static int cmd_loopback_off(const struct shell *sh, size_t argc, char **argv) {
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  usb_audio_bridge_set_loopback(false);
  shell_print(sh, "loopback disabled");
  return 0;
}

static int cmd_loopback_status(const struct shell *sh, size_t argc, char **argv) {
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  shell_print(sh, "loopback: %s", usb_audio_bridge_get_loopback() ? "on" : "off");
  return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(loopback_subcmds, SHELL_CMD(on, NULL, "Enable internal UAC2 loopback (SA818 bypass)", cmd_loopback_on),
                               SHELL_CMD(off, NULL, "Disable internal UAC2 loopback", cmd_loopback_off),
                               SHELL_CMD(status, NULL, "Show loopback state", cmd_loopback_status), SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(loopback, &loopback_subcmds, "Test-only UAC2 internal loopback (SA818 bypass)", NULL);
