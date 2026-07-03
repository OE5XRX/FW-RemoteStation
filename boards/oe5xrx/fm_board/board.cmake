# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

# FM Board Runner Configuration
# Configures flash/debug runners for STM32U575CIT target

board_runner_args(jlink "--device=STM32U575CIT" "--reset-after-load")
board_runner_args(pyocd "--target=stm32u575citx")
board_runner_args(pyocd "--flash-opt=-O connect_mode=under-reset")

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
