
find_program(cppcheck_exe NAMES cppcheck REQUIRED)
set(cppcheck_opts --enable=all --inline-suppr --error-exitcode=1 --suppressions-list=${PROJECT_SOURCE_DIR}/cppcheck.supp)
set(CMAKE_C_CPPCHECK ${cppcheck_exe} --std=c11 ${cppcheck_opts})
set(CMAKE_CXX_CPPCHECK ${cppcheck_exe} --std=c++20 ${cppcheck_opts})

add_executable(
  test-${PROJECT_NAME}
  fixed_string_test.cpp
  history_test.cpp
  ring_buffer_test.cpp
)

target_link_libraries(
  test-${PROJECT_NAME} PUBLIC
  ${PROJECT_NAME}
)

add_test(
  NAME test-${PROJECT_NAME}
  COMMAND test-${PROJECT_NAME}
)
