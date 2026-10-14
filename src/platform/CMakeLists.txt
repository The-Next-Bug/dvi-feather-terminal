# The RP2350 underneath: the terminal UART (serial.c), the status LED
# (led.c), VIDEO_DIAG's UART diagnostics (diag.c), the settings' flash
# sectors (nvflash.c), the firmware update's staging and commit
# (fwupdate.c) and the PSRAM (extram.c, PSRAM_BANKS builds).
add_library(dvt_platform STATIC
  diag.c
  extram.c
  fwupdate.c
  led.c
  nvflash.c
  serial.c
)
target_include_directories(dvt_platform PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(dvt_platform
  PUBLIC
    hardware_uart_headers
  PRIVATE
    dvt_board
    dvt_common
    dvt_settings
    fatfs
    hardware_clocks_headers
    hardware_flash_headers
    hardware_gpio_headers
    hardware_irq_headers
    hardware_structs
    hardware_sync_headers
    hardware_timer_headers
    hardware_watchdog_headers
    pico_bootrom_headers
    pico_stdio_headers
    pico_stdlib_headers
)
dvt_compile_options(dvt_platform)
if(PSRAM_BANKS)
  target_link_libraries(dvt_platform PRIVATE hardware_psram_headers)
endif()
