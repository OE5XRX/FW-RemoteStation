# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

# SA818 Radio Driver CMake Build Configuration
# Builds core driver, AT command handler, audio subsystem, and shell interface

zephyr_library()

# Core driver modules
zephyr_library_sources(
  src/sa818_core.cpp
  src/sa818_at.cpp
  src/sa818_audio.cpp
  src/sa818_audio_stream.cpp
)

# Shell interface (optional)
zephyr_library_sources_ifdef(CONFIG_SHELL src/sa818_shell.cpp)

# Include directories
zephyr_include_directories(include)
zephyr_library_include_directories(src)
