# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

# SA818 Radio Driver CMake Build Configuration
# Builds core driver, AT command handler, audio subsystem, and shell interface

zephyr_library()
zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR})
zephyr_library_sources(
  sa818_core.cpp
  sa818_at.cpp
  sa818_audio.cpp
  sa818_audio_stream.cpp
)

zephyr_library_sources_ifdef(
  CONFIG_SA818_SHELL
  sa818_shell.cpp)
