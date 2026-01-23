# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

# Radio Drivers CMake Configuration

add_subdirectory_ifdef(CONFIG_SA818 sa818)
