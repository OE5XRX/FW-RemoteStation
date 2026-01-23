# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (c) 2025 OE5XRX

zephyr_library()
zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR})
zephyr_library_sources(wav_dac.cpp)
