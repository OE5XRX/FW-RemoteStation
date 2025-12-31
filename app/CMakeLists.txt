cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C CXX ASM)

include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
target_sources(app PRIVATE src/main.cpp)

# USB Audio Bridge (application-level integration)
if(CONFIG_USBD_AUDIO2_CLASS)
  target_sources(app PRIVATE src/usb_audio_bridge.cpp)
endif()

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
