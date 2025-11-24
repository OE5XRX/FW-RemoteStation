# -------------------
# HAL library
# -------------------

string(TOLOWER startup_${MCU}.s STARTUP_FILE_NAME)
set(MCU_DIR CMSIS-DEVICE-F3)
set(CMSIS_DIR CMSIS/CMSIS)
set(STARTUP_FILE ${MCU_DIR}/Source/Templates/gcc/${STARTUP_FILE_NAME})
set(SYSTEM_FILE ${MCU_DIR}/Source/Templates/system_stm32f3xx.c)
set(HAL_DIR STM32F3xx_HAL_Driver)

file(GLOB HAL_SRC "${HAL_DIR}/Src/*.c")
list(FILTER HAL_SRC EXCLUDE REGEX ".*template\\.c$")

add_library(stm32_hal STATIC ${HAL_SRC} ${SYSTEM_FILE} ${STARTUP_FILE})

target_include_directories(stm32_hal PUBLIC . ${HAL_DIR}/Inc ${MCU_DIR}/Include ${CMSIS_DIR}/Core/Include)
target_compile_definitions(stm32_hal PUBLIC ${MCU} HSE_VALUE=${HSE_VAL})
target_compile_options(stm32_hal PRIVATE -Wno-error -w)

# -------------------
# Tiny USB
# -------------------
file(GLOB_RECURSE TINYUSB_SRC "tinyusb/src/*.c")
add_library(tinyusb STATIC ${TINYUSB_SRC})

target_include_directories(tinyusb PUBLIC . tinyusb/src)
target_compile_definitions(tinyusb PUBLIC CFG_TUSB_MCU=${CFG_TUSB_MCU})
target_link_libraries(tinyusb PUBLIC freertos_config freertos_kernel)
target_compile_options(tinyusb PRIVATE -Wno-error -w)

# -------------------
# FreeRTOS
# -------------------
set(FREERTOS_PORT GCC_ARM_CM4F CACHE STRING "")
