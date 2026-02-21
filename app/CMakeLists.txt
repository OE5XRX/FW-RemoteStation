cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C CXX ASM)

target_sources(app PRIVATE
  src/main.cpp
  src/audio_bridge/buffer.cpp
)

# USB and UAC2 configuration only for real hardware
if (NOT CONFIG_BOARD_NATIVE_SIM)
  target_sources(app PRIVATE
    src/usb_config.cpp
    src/uac2_simple.c
  )
endif()

#if (CONFIG_USB_DEVICE_STACK_NEXT)
#    target_sources(app PRIVATE
#        src/audio_bridge/bridge.cpp
#    )
#    target_include_directories(app PRIVATE src/audio_bridge)
#endif()

if (CONFIG_BOARD_NATIVE_SIM)
    target_sources(app PRIVATE
    src/sim_audio/sample_clock.cpp
    src/sim_audio/adc_sink.cpp
    src/sim_audio/sine_source.cpp
    src/sim_audio/wav_source.cpp
    src/sim_audio/audio_pipeline.cpp
    src/sim_audio/shell_wav.cpp
  )
endif()
