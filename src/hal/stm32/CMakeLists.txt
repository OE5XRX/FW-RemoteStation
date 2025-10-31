target_sources(
  ${PROJECT_NAME} PRIVATE
  freertos_hook.cpp
  syscalls.cpp
  heap_4.c
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
