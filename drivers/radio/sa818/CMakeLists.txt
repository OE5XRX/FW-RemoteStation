# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

# SA818 Radio Driver CMake Build Configuration
# Builds the core driver, AT command handler, and shell interface. The driver is
# audio-agnostic: it only does AT/UART control + control GPIOs.

zephyr_library()
zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR})
zephyr_library_sources(
  sa818_core.cpp
  sa818_at.cpp
)

zephyr_library_sources_ifdef(
  CONFIG_SA818_SHELL
  sa818_shell.cpp)
