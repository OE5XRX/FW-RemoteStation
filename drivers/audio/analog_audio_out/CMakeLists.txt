# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later
zephyr_library()
zephyr_library_sources(analog_audio_out.c dac_pcm.c)
zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../include)
