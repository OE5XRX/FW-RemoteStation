# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

zephyr_library()
zephyr_library_include_directories_ifdef(
  CONFIG_MODULE_SA818
  ${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/radio/sa818)
zephyr_library_sources_ifdef(CONFIG_MODULE_SA818 devices/sa818/sa818_module.cpp)

# Compile-time stack-frame guard for the module shell handlers.
#
# The `module` shell commands run on the shell thread, whose stack is only
# CONFIG_SHELL_STACK_SIZE (2048) bytes. A large command-response buffer placed on
# that stack overflows it and hard-faults on real hardware (Cortex-M33 PSPLIM
# guard) -- but is INVISIBLE on native_sim, because the POSIX arch runs each
# Zephyr thread on a host pthread stack (megabytes), so neither a runtime overflow
# nor STACK_SENTINEL ever triggers. That is why this class of bug was hardware-
# only. `-Werror=stack-usage=N` moves the check to COMPILE time, where it is
# arch-independent: any function in this library whose frame exceeds N bytes fails
# the build -- on native_sim CI just as on the hardware build. The response
# buffers therefore have to live in .bss (`static`), not on the stack. Budget is
# well under the 2048 shell stack yet far above the module's real frames (~hundreds
# of bytes); `=` (not the global -Werror) makes it an error regardless of
# CONFIG_COMPILER_WARNINGS_AS_ERRORS.
zephyr_library_compile_options(-Werror=stack-usage=1024)
