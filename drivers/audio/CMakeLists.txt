# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later
add_subdirectory_ifdef(CONFIG_ANALOG_AUDIO_IN analog_audio_in)
add_subdirectory_ifdef(CONFIG_ANALOG_AUDIO_OUT analog_audio_out)
