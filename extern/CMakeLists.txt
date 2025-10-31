message(PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
message(CMAKE_CROSSCOMPILING="${CMAKE_CROSSCOMPILING}")


if(CMAKE_CROSSCOMPILING MATCHES TRUE)
  add_subdirectory(arm)
else()
  add_subdirectory(host)
endif()

add_subdirectory(common)
