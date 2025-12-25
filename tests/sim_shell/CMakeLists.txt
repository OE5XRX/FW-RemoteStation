cmake_minimum_required(VERSION 3.20.0)

set(DTC_OVERLAY_FILE
  "${CMAKE_CURRENT_LIST_DIR}/../../app/boards/native_sim_native_64.overlay"
  CACHE STRING "" FORCE
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fm_sim_shell)

target_sources(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/sim_audio/sample_clock.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/sim_audio/adc_sink.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/sim_audio/sine_source.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/sim_audio/wav_source.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/sim_audio/audio_pipeline.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/sim_audio/shell_wav.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/main.cpp
)
