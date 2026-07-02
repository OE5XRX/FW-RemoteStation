# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

zephyr_library()
zephyr_library_include_directories_ifdef(
  CONFIG_MODULE_SA818
  ${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/radio/sa818)
zephyr_library_sources_ifdef(CONFIG_MODULE_SA818 devices/sa818/sa818_module.cpp)
