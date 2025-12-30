# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (c) 2025 OE5XRX

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_DAC_WAV wav_dac.cpp)
