cmake_minimum_required(VERSION 3.26)

project(
  OE5XRX
  VERSION 0.1
  DESCRIPTION "Firmware for the Remote Station Project"
  LANGUAGES ASM C CXX
)

include(CTest)

# Option to force building tests even when cross-compiling
option(UNITTEST "Build unit tests for host" OFF)

# Default build type if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting default build type to 'Debug'")
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -Werror)

# Export compile commands for editors/tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Ensure sources see the UNITTEST preprocessor macro when UNITTEST option is enabled
if(UNITTEST)
  add_compile_definitions(UNITTEST)
endif()

add_subdirectory(extern)

# -------------------
# Main elf / library
# -------------------
add_subdirectory(src)

# Build tests when UNITTEST is ON or when building for host
if(UNITTEST OR (CMAKE_CROSSCOMPILING MATCHES FALSE))
  add_subdirectory(test)
endif()


# Add additional files to the make clean
set_property(
  DIRECTORY
  PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${PROJECT_NAME}.map
           ${CMAKE_CURRENT_BINARY_DIR}/html ${CMAKE_CURRENT_BINARY_DIR}/latex)

# flash/doc targets remain unchanged
add_custom_target(flash DEPENDS ${PROJECT_NAME})
add_custom_command(
  TARGET flash
  POST_BUILD
  COMMAND
    echo
    "Starting GDB client and loading ${PROJECT_NAME}.elf dashboard to $(GDB_TTY)"
  COMMAND ${CMAKE_C_GDB} ${PROJECT_NAME}.elf -ex "target extended :3333" -ex
          "dashboard -output $(GDB_TTY)" -ex "load ${PROJECT_NAME}.elf")

add_custom_target(flash_dfu DEPENDS ${PROJECT_NAME})
add_custom_command(
  TARGET flash_dfu
  POST_BUILD
  COMMAND dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D ${PROJECT_NAME}.bin)
