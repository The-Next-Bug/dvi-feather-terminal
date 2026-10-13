cmake_minimum_required(VERSION 3.13)

# Default to the submodules and the toolchain installed by
# scripts/setup-toolchain.sh; any of these can be overridden on the command
# line or from the environment.
set(TOOLCHAIN_DIR "${CMAKE_CURRENT_LIST_DIR}/.toolchain")
if(NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
  set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/lib/pico-sdk")
endif()
if(NOT DEFINED PICO_TOOLCHAIN_PATH AND NOT DEFINED ENV{PICO_TOOLCHAIN_PATH}
    AND EXISTS "${TOOLCHAIN_DIR}/arm-none-eabi/bin")
  set(PICO_TOOLCHAIN_PATH "${TOOLCHAIN_DIR}/arm-none-eabi")
endif()
if(NOT DEFINED picotool_DIR AND EXISTS "${TOOLCHAIN_DIR}/picotool/picotool")
  set(picotool_DIR "${TOOLCHAIN_DIR}/picotool/picotool")
endif()

# The board: boards/<PICO_BOARD>.h, a pico-sdk board header with the
# terminal's pins (boards/example.h; checked by src/board.h). RP2350 only:
# the RP2040 ran out of SRAM and could not run the USB keyboard at the DVI
# clock (bd dmt.20, dmt.29).
set(PICO_BOARD pico2-dvi-sock CACHE STRING "Board: boards/<PICO_BOARD>.h (an RP2350 board)")
list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/boards")
option(STARTUP_LOGO "Show the logo (LOGO_IMAGE) for a few seconds at power-on/reset" ON)
# Monitors take a few seconds to lock onto the signal after power-on, which
# comes out of this.
set(LOGO_MS 10000 CACHE STRING "How long the startup logo shows, in ms")
# Experimental (bd dmt.25): leave the logo's 19 KB in flash and read it
# through XIP when it is shown, rather than keep a copy in SRAM. Needs the
# flash read clock that nvflash_retime sets after the DVI clock change.
option(LOGO_IN_FLASH "Keep the startup logo data in flash instead of SRAM" ON)
option(STARTUP_DEMO "Then cycle the 80/132-column demo until UART0 receives data or a key is pressed" OFF)
option(VIDEO_DIAG "Bring-up diagnostics on UART0 TX: boot markers, core 1 timing, faults" OFF)
# Run from SRAM: the boot-time XIP setup is chosen for clk_sys <= 150 MHz, and
# at the 400 MHz DVI clock flash execution is unreliable (on the RP2040 it hung
# above 252 MHz). See bd dvi-feather-terminal-dmt.10.
# Off by default: needs the two handshake wires (see README, "Flow control").
option(SERIAL_FLOW_CONTROL "RTS/CTS on the terminal UART (the board's BOARD_UART_RTS_PIN/CTS_PIN)" OFF)
option(COPY_TO_RAM "Run the whole program from SRAM instead of flash" ON)
# Video timing (src/hstx_dvi.h): 800x600 at 60 Hz needs clk_hstx at 200 MHz,
# beyond its 150 MHz rating; the others are fallbacks with slower clocks.
set(HSTX_MODE 0 CACHE STRING "HSTX video mode: 0 800x600@60, 1 800x600@56, 2 800x600@60 reduced blanking")
# Z80 CP/M machine (Mode setting): 48 KB memory banks besides the 64 KB of
# bank 0. CP/M 2.2 needs none, CP/M 3 one; MP/M wants as many as it was
# generated for.
set(MACHINE_BANKS 2 CACHE STRING "Z80 machine: number of 48 KB banks (MMU port 64)")

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(dvi_feather_terminal C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

if(NOT PICO_PLATFORM MATCHES "^rp2350")
  message(FATAL_ERROR "PICO_BOARD ${PICO_BOARD} is ${PICO_PLATFORM}: only RP2350 boards are supported")
endif()

set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
include(cmake/dvt.cmake)

# The libraries (src/<name>, bd gr1), the RP2350 ones too. They do not
# link the SDK's libraries, so they get here the code generation options
# those give the code that does: pico_standard_link's -ffunction-sections
# -fdata-sections (without them --gc-sections cannot drop unused functions,
# which then take SRAM) and pico_float's -fno-math-errno.
set(DVT_FIRMWARE ON)
set(DVT_C_OPTIONS
  $<TARGET_PROPERTY:pico_standard_link,INTERFACE_COMPILE_OPTIONS>
  -fno-math-errno)
add_subdirectory(src)
# Firmware-only settings of portable libraries: the fonts and the logo
# stay in flash (read through XIP by core 0).
target_compile_definitions(dvt_screen PUBLIC FONT_FLASH=1)
target_compile_definitions(dvt_render PRIVATE LOGO_IN_FLASH=$<BOOL:${LOGO_IN_FLASH}>)
# The built-in disk images stay in flash too (read by core 0 when
# installed); the Z80 machine's banks.
target_compile_definitions(dvt_disks PUBLIC DISKIMG_IN_FLASH=1)
target_compile_definitions(dvt_z80pack PUBLIC MACHINE_BANKS=${MACHINE_BANKS})
target_compile_definitions(dvt_board INTERFACE SERIAL_FLOW_CONTROL=$<BOOL:${SERIAL_FLOW_CONTROL}>)
target_compile_definitions(dvt_platform PUBLIC VIDEO_DIAG=$<BOOL:${VIDEO_DIAG}>)
target_compile_definitions(dvt_video PUBLIC HSTX_MODE=${HSTX_MODE})

# Runs on every build; rewrites term_build.h only when git state changes.
add_custom_target(build_info
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}
          -DOUTPUT=${GENERATED}/term_build.h
          -P ${CMAKE_CURRENT_LIST_DIR}/cmake/build_info.cmake
  BYPRODUCTS ${GENERATED}/term_build.h
  VERBATIM)

add_executable(dvi_terminal
  src/main.c
)
add_dependencies(dvi_terminal build_info)

target_include_directories(dvi_terminal PRIVATE
  src
  ${GENERATED}
)

# Warnings for our sources only, not the SDK's.
set_source_files_properties(src/main.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
# TinyUSB's MSC host is cold code (it runs only while a USB stick comes or
# goes, or files move): small rather than fast, as all code takes SRAM.
set_property(SOURCE
  ${PICO_TINYUSB_PATH}/src/class/msc/msc_host.c APPEND PROPERTY COMPILE_OPTIONS "-Os")

target_compile_definitions(dvi_terminal PRIVATE
  HSTX_MODE=${HSTX_MODE}
  STARTUP_LOGO=$<BOOL:${STARTUP_LOGO}>
  LOGO_MS=${LOGO_MS}
  LOGO_IN_FLASH=$<BOOL:${LOGO_IN_FLASH}>
  STARTUP_DEMO=$<BOOL:${STARTUP_DEMO}>
  VIDEO_DIAG=$<BOOL:${VIDEO_DIAG}>
  SERIAL_FLOW_CONTROL=$<BOOL:${SERIAL_FLOW_CONTROL}>
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
  dvt_machine
  dvt_platform
  dvt_settings
  dvt_storage
  dvt_usb
  dvt_video
  dvt_input
  dvt_render
  dvt_screen
  dvt_term
  dvt_ui
  pico_stdlib
  pico_multicore
  hardware_flash
  hardware_uart
  hardware_dma
  hardware_spi
  hardware_vreg
  hardware_watchdog
)

# The USB host: keyboards and sticks on the native USB port (TinyUSB's
# sources, built here, find tusb_config.h through dvt_usb).
target_link_libraries(dvi_terminal tinyusb_host)

# UART0 belongs to the terminal (serial.c): no stdio on it or on USB.
if(VIDEO_DIAG)
  # Stack overflows fault (and are reported) instead of corrupting memory.
  target_compile_definitions(dvi_terminal PRIVATE PICO_USE_STACK_GUARDS=1)
endif()
if(COPY_TO_RAM)
  pico_set_binary_type(dvi_terminal copy_to_ram)
  # Cold code runs from flash (XIP) to save SRAM: ld/ (bd qur.24).
  pico_add_linker_script_override_path(dvi_terminal ${CMAKE_CURRENT_LIST_DIR}/ld GLOB_FILES)
endif()

pico_enable_stdio_uart(dvi_terminal 0)
pico_enable_stdio_usb(dvi_terminal 0)
pico_add_extra_outputs(dvi_terminal)

# The programs: the terminal above; the HSTX test pattern.
add_subdirectory(apps/hstx_test)
