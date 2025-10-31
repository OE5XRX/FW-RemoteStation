
# -------------------
# cpputest
# -------------------
find_package(PkgConfig REQUIRED)
pkg_search_module(CPPUTEST REQUIRED cpputest>=3.8)
message(STATUS "Found CppUTest version ${CPPUTEST_VERSION}")

# -------------------
# fake-timers
# -------------------
add_library(fake-timers INTERFACE)
target_include_directories(fake-timers INTERFACE fake-timers/include)

# -------------------
# cpputest for freertos
# -------------------
add_library(
  freertos_kernel
    cpputest-for-freertos/cpputest-for-freertos-lib/src/cpputest_for_freertos_task.cpp
    cpputest-for-freertos/cpputest-for-freertos-lib/src/cpputest_for_freertos_queue.cpp
    cpputest-for-freertos/cpputest-for-freertos-lib/src/cpputest_for_freertos_queue_set.cpp
    cpputest-for-freertos/cpputest-for-freertos-lib/src/cpputest_for_freertos_assert.cpp
    cpputest-for-freertos/cpputest-for-freertos-lib/src/cpputest_for_freertos_timers.cpp
    cpputest-for-freertos/cpputest-for-freertos-lib/src/cpputest_main.cpp
    cpputest-for-freertos/cpputest-for-freertos-lib/src/cpputest_for_freertos_semaphore.cpp
    cpputest-for-freertos/cpputest-for-freertos-lib/src/cpputest_for_freertos_mutex.cpp
    cpputest-for-freertos/cpputest-for-freertos-lib/include/cpputest_for_freertos_lib.hpp
)

target_include_directories(
  freertos_kernel
  PUBLIC
    cpputest-for-freertos/cpputest-for-freertos-lib/include
    FreeRTOS-config
    ../arm/FreeRTOS/include
    ${CPPUTEST_INCLUDE_DIRS}
)

link_directories(${CPPUTEST_LIBRARIES})

target_link_libraries(
  freertos_kernel
  ${CPPUTEST_LDFLAGS}
  fake-timers
)

#target_link_libraries(cpputest-for-freertos-lib FreeRTOS)

# -------------------
# FreeRTOS
# -------------------

#add_library(FreeRTOS INTERFACE)
#target_include_directories(FreeRTOS INTERFACE ../arm/FreeRTOS/include)
#target_link_libraries(FreeRTOS cpputest-for-freertos-lib)
