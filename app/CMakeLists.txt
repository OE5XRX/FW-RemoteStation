cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C CXX ASM)

target_sources(app PRIVATE src/version_shell.cpp)

if (CONFIG_ETL)
    target_sources(app PRIVATE src/etl_error_handler.cpp)
endif()

if (CONFIG_BOARD_NATIVE_SIM)
    # native_sim has no USB; audio is exercised via emulated ADC/DAC.
    target_sources(app PRIVATE
        src/main.cpp
        src/sim_audio/sample_clock.cpp
        src/sim_audio/adc_sink.cpp
        src/sim_audio/sine_source.cpp
        src/sim_audio/wav_source.cpp
        src/sim_audio/audio_pipeline.cpp
        src/sim_audio/shell_wav.cpp
    )
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
        src/feedback.cpp
    )
else()
    target_sources(app PRIVATE src/main.cpp)
endif()
