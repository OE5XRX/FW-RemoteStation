if(UNITTEST_BUILD OR HOST_BUILD)
  add_subdirectory(host)
else()
  add_subdirectory(stm32)
endif()

target_sources(
  ${PROJECT_NAME} PRIVATE
  cdc_base.cpp
  cat24c32.cpp
)
