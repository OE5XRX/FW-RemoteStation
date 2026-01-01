cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fm_usb_audio_test)

# Include USB sample init
include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)

# Application sources
target_sources(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/main_usb_audio.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/usb_audio_bridge.cpp
)
