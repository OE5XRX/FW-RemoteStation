cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C CXX ASM)

target_sources(app PRIVATE src/version_shell.cpp)

if (CONFIG_ETL)
    target_sources(app PRIVATE src/etl_error_handler.cpp)
endif()

if (CONFIG_BOARD_NATIVE_SIM)
    # native_sim has no USB and no hardware-timed audio (analog-audio-in/out are
    # STM32-only); the SA818 driver runs AT/UART + control GPIOs only.
    target_sources(app PRIVATE src/main.cpp)
elseif (CONFIG_USB_DEVICE_STACK_NEXT)
    # Real hardware: 3-class USB composite (CDC-ACM + UAC2 + DFU).
    include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
    # SA818 driver public headers (<sa818/sa818.h> etc.) are only exposed via the
    # driver's own library-private include path; the app target needs it too,
    # same as subsys/module/CMakeLists.txt does for CONFIG_MODULE_SA818 and
    # tests/usb_audio/CMakeLists.txt does for this same pair of sources.
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../drivers/radio/sa818
    )
    target_sources(app PRIVATE
        src/main_usb_audio.cpp
        src/usb_audio_bridge.cpp
        src/audio_stream.cpp
        src/feedback.cpp
        src/boot_confirm/health_gate.cpp
        src/boot_confirm/boot_confirm_fm.cpp
    )
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/boot_confirm)
else()
    target_sources(app PRIVATE src/main.cpp)
endif()
