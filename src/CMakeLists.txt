find_program(cppcheck_exe NAMES cppcheck REQUIRED)
set(cppcheck_opts --enable=all --inline-suppr --error-exitcode=1 --suppressions-list=${PROJECT_SOURCE_DIR}/cppcheck.supp -D __GNUC__)
set(CMAKE_C_CPPCHECK ${cppcheck_exe} --std=c11 ${cppcheck_opts})
set(CMAKE_CXX_CPPCHECK ${cppcheck_exe} --std=c++20 ${cppcheck_opts})

# Liste der Quell-Dateien zentral verwalten
set(SRC_FILES
  main.cpp
  shell/shell.cpp
  shell/history.cpp
  shell/log.cpp
)

if(CMAKE_CROSSCOMPILING)
  add_executable(
    ${PROJECT_NAME}
    ${SRC_FILES}
  )
else()
  add_library(
    ${PROJECT_NAME} STATIC
    ${SRC_FILES}
  )
endif()

add_subdirectory(hal)

target_include_directories(
  ${PROJECT_NAME} PUBLIC
  .
)

target_link_libraries(
  ${PROJECT_NAME} PUBLIC
  FreeRTOS-Cpp
)

target_compile_definitions(
  ${PROJECT_NAME} PUBLIC
  ${MCU}
  HSE_VALUE=${HSE_VAL}
  CFG_TUSB_MCU=${CFG_TUSB_MCU}
  ${UNITTEST_BUILD}
)

if(CMAKE_CROSSCOMPILING)
  # Convert output to hex and binary
  add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_PROJECT_NAME}.hex)

  # Convert to bin file
  add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_PROJECT_NAME}.bin)
endif()
