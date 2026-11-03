# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(boot_key_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../subsys/module)
target_sources(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src/main.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../subsys/module/boot_key.cpp
)

# imgtool-signed MCUboot images (see fixtures/make_fixtures.sh), embedded as
# C byte lists.
foreach(fixture dev-signed other-signed prot-signed)
  generate_inc_file_for_target(app
    ${CMAKE_CURRENT_LIST_DIR}/fixtures/${fixture}.bin
    ${ZEPHYR_BINARY_DIR}/include/generated/${fixture}.inc)
endforeach()
