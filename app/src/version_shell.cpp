/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * `version` shell command — reports the stamped firmware release version.
 * The release job writes app/VERSION as MAJOR=YY MINOR=MM PATCHLEVEL=DD
 * TWEAK=NN, so the runtime string matches the release tag and asset names
 * exactly. Zero-padding is display-only; app/VERSION stores plain ints.
 */
#include <zephyr/app_version.h>
#include <zephyr/shell/shell.h>

static int cmd_version(const struct shell *sh, size_t argc, char **argv) {
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  shell_print(sh, "APP-VERSION %02u.%02u.%02u-%02u", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL, APP_TWEAK);
  return 0;
}

SHELL_CMD_REGISTER(version, NULL, "Print the stamped firmware release version (YY.MM.DD-NN)", cmd_version);
