# The terminal (dvi_terminal.uf2): main.c and its pieces (terminal.h) over
# the libraries in src/. Its outputs go next to the build's top, where CI,
# scripts/check_sram_only.py and test_uf2 look (pico_add_extra_outputs
# writes the UF2 to ARCHIVE_OUTPUT_DIRECTORY).

set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
# Runs on every build; rewrites term_build.h only when git state changes.
add_custom_target(build_info
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
          -DOUTPUT=${GENERATED}/term_build.h
          -P ${PROJECT_SOURCE_DIR}/cmake/build_info.cmake
  BYPRODUCTS ${GENERATED}/term_build.h
  VERBATIM)

set(TERMINAL_SOURCES
  cpm_glue.c
  input_glue.c
  main.c
  setup_glue.c
  status.c
)
add_executable(dvi_terminal ${TERMINAL_SOURCES})
add_dependencies(dvi_terminal build_info)
set_target_properties(dvi_terminal PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(dvi_terminal PRIVATE ${GENERATED})

# Warnings for our sources only, not the SDK's (built here too).
set_source_files_properties(${TERMINAL_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
# TinyUSB's MSC host is cold code (it runs only while a USB stick comes or
# goes, or files move): small rather than fast, as all code takes SRAM.
set_property(SOURCE
  ${PICO_TINYUSB_PATH}/src/class/msc/msc_host.c APPEND PROPERTY COMPILE_OPTIONS "-Os")

target_compile_definitions(dvi_terminal PRIVATE
  STARTUP_LOGO=$<BOOL:${STARTUP_LOGO}>
  LOGO_MS=${LOGO_MS}
  STARTUP_DEMO=$<BOOL:${STARTUP_DEMO}>
  # The compose loop needs very little stack.
  PICO_CORE1_STACK_SIZE=0x200
  # Nothing prints floating point: its printf code (and the soft-float it
  # pulls in) would only take SRAM.
  PICO_PRINTF_SUPPORT_FLOAT=0
  PICO_PRINTF_SUPPORT_EXPONENTIAL=0
)

target_link_libraries(dvi_terminal
  dvt_board
  dvt_common
  dvt_disks
  dvt_input
  dvt_machine
  dvt_platform
  dvt_render
  dvt_screen
  dvt_settings
  dvt_storage
  dvt_term
  dvt_ui
  dvt_usb
  dvt_video
  # The SDK's libraries, which the dvt_ ones compile against.
  hardware_dma
  hardware_flash
  hardware_spi
  hardware_uart
  hardware_vreg
  hardware_watchdog
  pico_multicore
  pico_stdlib
  # The USB host: keyboards and sticks on the native USB port (TinyUSB's
  # sources find tusb_config.h through dvt_usb).
  tinyusb_host
)

# The SDK's PSRAM support: found and set up at boot (runtime init), the
# timing set again after the clock change (extram_retime).
if(PSRAM_BANKS)
  target_link_libraries(dvi_terminal hardware_psram)
endif()

# UART0 belongs to the terminal (serial.c): no stdio on it or on USB.
if(VIDEO_DIAG)
  # Stack overflows fault (and are reported) instead of corrupting memory.
  target_compile_definitions(dvi_terminal PRIVATE PICO_USE_STACK_GUARDS=1)
endif()
if(COPY_TO_RAM)
  pico_set_binary_type(dvi_terminal copy_to_ram)
  # Cold code runs from flash (XIP) to save SRAM: ld/ (bd qur.24).
  pico_add_linker_script_override_path(dvi_terminal ${PROJECT_SOURCE_DIR}/ld GLOB_FILES)
endif()

pico_enable_stdio_uart(dvi_terminal 0)
pico_enable_stdio_usb(dvi_terminal 0)
pico_add_extra_outputs(dvi_terminal)
