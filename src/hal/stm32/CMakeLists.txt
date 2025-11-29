target_sources(
  ${PROJECT_NAME} PRIVATE
  board.cpp
  cat24c32_stm32.cpp
  syscalls.cpp
  timer_stats.cpp
  uart.cpp
  usb/cdc.cpp
  usb/dfu.cpp
  usb/handler.cpp
  usb/tud.cpp
  usb/usb_descriptors.cpp
)

target_link_libraries(
  ${PROJECT_NAME} PUBLIC
  tinyusb
)

target_include_directories(${PROJECT_NAME} PRIVATE .)
