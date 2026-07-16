# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(unit_audio)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../app/src)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../drivers/audio/analog_audio_in)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../drivers/audio/analog_audio_out)

target_sources(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src/main.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/feedback.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../drivers/audio/analog_audio_in/adc_pcm.c
  ${CMAKE_CURRENT_LIST_DIR}/../../drivers/audio/analog_audio_out/dac_pcm.c
)
