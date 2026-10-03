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

# RP2350 only: the RP2040 ran out of SRAM and could not run the USB keyboard
# at the DVI clock (bd dmt.20, dmt.29).
set(PICO_BOARD pico2 CACHE STRING "pico-sdk board (an RP2350 board)")
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
option(SERIAL_FLOW_CONTROL "RTS/CTS on UART0 (GP3 RTS out, GP2 CTS in)" OFF)
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

option(USB_KEYBOARD "USB keyboard on the native USB port (TinyUSB host)" ON)
message(STATUS "USB_KEYBOARD: ${USB_KEYBOARD}")
message(STATUS "FONT_132: ${FONT_132}")

set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
include(cmake/fonts.cmake)
include(cmake/logo.cmake)
include(cmake/diskimg.cmake)
include(cmake/z80pack.cmake)

# Runs on every build; rewrites term_build.h only when git state changes.
add_custom_target(build_info
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}
          -DOUTPUT=${GENERATED}/term_build.h
          -P ${CMAKE_CURRENT_LIST_DIR}/cmake/build_info.cmake
  BYPRODUCTS ${GENERATED}/term_build.h
  VERBATIM)

add_executable(dvi_terminal
  src/crc32.c
  src/blank.c
  src/demo.c
  src/diag.c
  src/diskimg.c
  src/drivepanel.c
  src/drives.c
  src/fwupdate.c
  src/hstx_dvi.c
  src/keyname.c
  src/lz.c
  src/machine.c
  src/main.c
  src/monitor.c
  src/nvflash.c
  src/palette.c
  src/raster.c
  src/screen.c
  src/sdcard.c
  src/settings.c
  src/setup.c
  src/term.c
  src/serial.c
  src/splash.c
  src/uf2.c
  src/video.c
  src/vtparse.c
  ${FONT_SOURCES}
  ${DISKIMG_SOURCES}
  ${Z80PACK_SOURCES}
  ${FATFS_SOURCES}
)
add_dependencies(dvi_terminal build_info diskimg_data)

target_include_directories(dvi_terminal PRIVATE
  src
  ${GENERATED}
  ${Z80PACK_INCLUDE_DIRS}
  ${FATFS_INCLUDE_DIR}
)

# Warnings for our sources only, not the SDK's.
set_source_files_properties(src/blank.c src/crc32.c src/demo.c src/diag.c src/diskimg.c src/drivepanel.c src/drives.c src/fwupdate.c src/hstx_dvi.c src/keyboard.c
  src/keyname.c src/lz.c src/machine.c src/main.c src/monitor.c src/nvflash.c src/palette.c src/raster.c src/screen.c src/sdcard.c
  src/settings.c src/setup.c src/splash.c src/term.c src/serial.c src/uf2.c src/usbkbd.c src/video.c src/vtparse.c
  PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

target_compile_definitions(dvi_terminal PRIVATE
  HSTX_MODE=${HSTX_MODE}
  STARTUP_LOGO=$<BOOL:${STARTUP_LOGO}>
  LOGO_MS=${LOGO_MS}
  LOGO_IN_FLASH=$<BOOL:${LOGO_IN_FLASH}>
  STARTUP_DEMO=$<BOOL:${STARTUP_DEMO}>
  VIDEO_DIAG=$<BOOL:${VIDEO_DIAG}>
  USB_KEYBOARD=$<BOOL:${USB_KEYBOARD}>
  SERIAL_FLOW_CONTROL=$<BOOL:${SERIAL_FLOW_CONTROL}>
  MACHINE_BANKS=${MACHINE_BANKS}
  ${FONT_DEFINITIONS}
  # The built-in disk images stay in flash (read by core 0 when installed).
  DISKIMG_IN_FLASH=1
  # The compose loop needs very little stack.
  PICO_CORE1_STACK_SIZE=0x200
)

target_link_libraries(dvi_terminal
  pico_stdlib
  pico_multicore
  hardware_flash
  hardware_uart
  hardware_dma
  hardware_spi
  hardware_vreg
  hardware_watchdog
)

if(STARTUP_LOGO)
  target_sources(dvi_terminal PRIVATE ${LOGO_SOURCES})
endif()

if(USB_KEYBOARD)
  target_sources(dvi_terminal PRIVATE src/keyboard.c src/usbkbd.c)
  # tusb_config.h is in src/.
  target_link_libraries(dvi_terminal tinyusb_host)
endif()

# UART0 belongs to the terminal (serial.c): no stdio on it or on USB.
if(VIDEO_DIAG)
  # Stack overflows fault (and are reported) instead of corrupting memory.
  target_compile_definitions(dvi_terminal PRIVATE PICO_USE_STACK_GUARDS=1)
endif()
if(COPY_TO_RAM)
  pico_set_binary_type(dvi_terminal copy_to_ram)
endif()

pico_enable_stdio_uart(dvi_terminal 0)
pico_enable_stdio_usb(dvi_terminal 0)
pico_add_extra_outputs(dvi_terminal)

# HSTX DVI bring-up test pattern (bd dmt.8.1): hstx_test.uf2 next to the
# terminal. HSTX_MODE picks the timing (see src/hstx_dvi.h).
add_executable(hstx_test src/hstx_test.c src/hstx_dvi.c)
target_include_directories(hstx_test PRIVATE src)
set_source_files_properties(src/hstx_test.c src/hstx_dvi.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
target_compile_definitions(hstx_test PRIVATE HSTX_MODE=${HSTX_MODE})
target_link_libraries(hstx_test pico_stdlib hardware_dma hardware_vreg)
pico_set_binary_type(hstx_test copy_to_ram)
pico_enable_stdio_uart(hstx_test 1)
pico_enable_stdio_usb(hstx_test 0)
pico_add_extra_outputs(hstx_test)
