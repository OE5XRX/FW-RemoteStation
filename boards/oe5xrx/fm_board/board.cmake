# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

# FM Board Runner Configuration
# Configures flash/debug runners for STM32F302VC target

board_runner_args(jlink "--device=STM32F302VC" "--speed=4000")

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
