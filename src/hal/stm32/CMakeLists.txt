target_sources(
  ${PROJECT_NAME} PRIVATE
  syscalls.cpp
  timer_stats.cpp
  board.cpp
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
