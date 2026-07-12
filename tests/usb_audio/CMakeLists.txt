cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fm_usb_audio_test)

# Include USB sample init
include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)

# SA818 driver public headers (<sa818/sa818.h> etc.) are only exposed via the
# driver's own library-private include path; the app target needs it too,
# same as subsys/module/CMakeLists.txt does for CONFIG_MODULE_SA818.
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../../drivers/radio/sa818
)

# Application sources
target_sources(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/main_usb_audio.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/usb_audio_bridge.cpp
)
