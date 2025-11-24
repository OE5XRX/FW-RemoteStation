target_sources(
  ${PROJECT_NAME} PRIVATE
  board.cpp
  cdc_host.cpp
  cli.cpp
  uart.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE .)
