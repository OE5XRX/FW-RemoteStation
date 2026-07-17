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

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/boot_confirm
)

# Application sources. main_usb_audio.cpp unconditionally calls the health-gate
# entry points (boot_confirm_fm_start / boot_confirm_fm_usb_configured), so the
# boot_confirm sources must be compiled here too. dfu_mode.cpp is NOT needed:
# main only references it under CONFIG_BOOTLOADER_MCUBOOT, which this bare test
# build does not set.
target_sources(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/main_usb_audio.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/usb_audio_bridge.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/audio_stream.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/feedback.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/boot_confirm/health_gate.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/boot_confirm/boot_confirm_fm.cpp
)
