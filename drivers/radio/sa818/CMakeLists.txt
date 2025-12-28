zephyr_library()

# Core driver modules
zephyr_library_sources(
  src/sa818_core.cpp
  src/sa818_at.cpp
  src/sa818_audio.cpp
  src/sa818_shell.cpp
)

# Include directories
zephyr_include_directories(include)
zephyr_library_include_directories(src)
