cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(boot_confirm_test LANGUAGES CXX)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../app/src/boot_confirm)
target_sources(app PRIVATE
    src/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../app/src/boot_confirm/health_gate.cpp)
