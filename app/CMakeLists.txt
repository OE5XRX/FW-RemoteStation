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
        src/boot_confirm/health_gate.cpp
        src/boot_confirm/boot_confirm_fm.cpp
    )
    # The explicit-feedback PI regulator (feedback.cpp) is only wired in when the
    # OUT streaming interface does NOT use implicit feedback. With implicit-feedback
    # (fm_board default) the UAC2 class emits no feedback endpoint, so building it
    # into the app would be dead code -- keep the source out of the app build then.
    # (It stays unit-tested via tests/unit_audio regardless of this selection.)
    dt_nodelabel(as_iso_out_path NODELABEL "as_iso_out")
    if(as_iso_out_path)
        dt_prop(as_iso_out_implicit_fb PATH "${as_iso_out_path}" PROPERTY "implicit-feedback")
    endif()
    if(NOT as_iso_out_implicit_fb)
        target_sources(app PRIVATE src/feedback.cpp)
    endif()
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/boot_confirm)
    # DFU runtime->DFU-mode switch is prod-only (MCUboot); guard so the bare build never compiles it.
    if(CONFIG_BOOTLOADER_MCUBOOT)
        target_sources(app PRIVATE src/dfu_mode.cpp)
    endif()
    # Test-only bench loopback shell command (SA818 bypass); default-off Kconfig.
    if(CONFIG_FM_TEST_LOOPBACK)
        target_sources(app PRIVATE src/loopback_shell.cpp)
    endif()
else()
    target_sources(app PRIVATE src/main.cpp)
endif()
