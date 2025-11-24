message(PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
message(CMAKE_CROSSCOMPILING="${CMAKE_CROSSCOMPILING}")


if(CMAKE_CROSSCOMPILING)
  add_subdirectory(arm)
else()
  if(UNITTEST_BUILD)
    add_subdirectory(unittest)
  elseif(HOST_BUILD)
    add_subdirectory(host)
  endif()
endif()


if(CMAKE_CROSSCOMPILING OR HOST_BUILD)
  add_library(freertos_config INTERFACE)
  target_include_directories(freertos_config INTERFACE FreeRTOS-config)
  target_compile_definitions(freertos_config INTERFACE projCOVERAGE_TEST=0)

  if(CMAKE_CROSSCOMPILING)
    target_link_libraries(freertos_config INTERFACE stm32_hal)
  endif()

  add_subdirectory(FreeRTOS)
  target_compile_options(freertos_kernel PRIVATE -Wno-error -w)
  target_compile_options(freertos_kernel_port PRIVATE -Wno-error -w)
endif()

add_subdirectory(common)
