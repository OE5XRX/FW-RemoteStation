# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

# FM Board Runner Configuration
# Configures flash/debug runners for STM32U575CITx target

board_runner_args(pyocd "--target=stm32u575citx")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
