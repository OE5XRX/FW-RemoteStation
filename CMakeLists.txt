# Export the reusable, device-agnostic module framework headers (include/oe5xrx/...).
zephyr_include_directories(include)

add_subdirectory(drivers)
add_subdirectory(subsys/module)

# MCUboot image only (sysbuild builds it with this module too): IWDG setup that
# pauses while a debugger halts the core. CONFIG_MCUBOOT is set only in the
# bootloader image, never in the app.
if(CONFIG_MCUBOOT AND CONFIG_BOOT_WATCHDOG_INSTALL_TIMEOUT_AT_BOOT AND CONFIG_BOARD_FM_BOARD)
  add_subdirectory(subsys/mcuboot_wdt)
endif()
