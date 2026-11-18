# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

# MCUboot-image-only: replaces MCUboot's weak mcuboot_watchdog_setup() so the
# IWDG it arms before jumping to the app is frozen while a debugger halts the
# core (see mcuboot_watchdog.c).
zephyr_library()
zephyr_library_sources(mcuboot_watchdog.c)
