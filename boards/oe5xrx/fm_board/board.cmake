# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

# FM Board Runner Configuration
# Configures flash/debug runners for STM32F302VC target

board_runner_args(pyocd "--target=stm32f302vctx")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
