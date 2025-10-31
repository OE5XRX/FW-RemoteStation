
# -------------------
# FreeRTOS-Cpp
# -------------------
add_library(FreeRTOS-Cpp INTERFACE)
target_include_directories(FreeRTOS-Cpp INTERFACE FreeRTOS-Cpp/FreeRTOS-Cpp/include)
target_link_libraries(FreeRTOS-Cpp INTERFACE freertos_kernel)
