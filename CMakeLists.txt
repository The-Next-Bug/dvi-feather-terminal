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

set(PICO_BOARD adafruit_feather_rp2040 CACHE STRING "pico-sdk board")
option(STARTUP_LOGO "Show the logo (LOGO_IMAGE) for a few seconds at power-on/reset" ON)
# Monitors take a few seconds to lock onto the signal after power-on, which
# comes out of this.
set(LOGO_MS 10000 CACHE STRING "How long the startup logo shows, in ms")
option(STARTUP_DEMO "Then cycle the 80/132-column demo until UART0 receives data or a key is pressed" OFF)
option(VIDEO_DIAG "Bring-up diagnostics on UART0 TX: boot markers, core 1 timing, faults" OFF)
# Run from SRAM: the board's boot2 reads flash with 03h at clk_sys / 4, and at
# DVI clocks above 252 MHz (e.g. 67.5 MHz SCK at 270 MHz) flash execution
# hangs. PicoDVI's apps are built this way too (PICO_COPY_TO_RAM=1). See bd
# dvi-feather-terminal-dmt.10.
# Off by default: needs the two handshake wires (see README, "Flow control").
option(SERIAL_FLOW_CONTROL "RTS/CTS on UART0 (GP3 RTS out, GP2 CTS in: the STEMMA QT pins)" OFF)
option(COPY_TO_RAM "Run the whole program from SRAM instead of flash" ON)
# A PicoDVI serialiser config from common_dvi_pin_configs.h.
set(DVI_SERIAL_CONFIG adafruit_feather_dvi_cfg CACHE STRING "PicoDVI pin configuration")

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(dvi_feather_terminal C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

# USB keyboard (TinyUSB host on the native USB port): RP2350 only by default.
# On the Feather RP2040 DVI the firmware crashes with it (bd dmt.20); the
# RP2040 build leaves it out and keeps the RAM.
if(PICO_PLATFORM MATCHES "^rp2350")
  set(USB_KEYBOARD_DEFAULT ON)
else()
  set(USB_KEYBOARD_DEFAULT OFF)
endif()
option(USB_KEYBOARD "USB keyboard on the native USB port (TinyUSB host)" ${USB_KEYBOARD_DEFAULT})
message(STATUS "USB_KEYBOARD: ${USB_KEYBOARD} (platform ${PICO_PLATFORM})")

set(PICODVI "${CMAKE_CURRENT_LIST_DIR}/lib/PicoDVI/software")
add_subdirectory(${PICODVI}/libdvi libdvi)

set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
include(cmake/fonts.cmake)
include(cmake/logo.cmake)

# Runs on every build; rewrites term_build.h only when git state changes.
add_custom_target(build_info
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}
          -DOUTPUT=${GENERATED}/term_build.h
          -P ${CMAKE_CURRENT_LIST_DIR}/cmake/build_info.cmake
  BYPRODUCTS ${GENERATED}/term_build.h
  VERBATIM)

add_executable(dvi_terminal
  src/demo.c
  src/diag.c
  src/main.c
  src/raster.c
  src/screen.c
  src/term.c
  src/serial.c
  src/splash.c
  src/video.c
  src/vtparse.c
  ${FONT_SOURCES}
  ${PICODVI}/apps/colour_terminal/tmds_encode_font_2bpp.S
)
add_dependencies(dvi_terminal build_info)

target_include_directories(dvi_terminal PRIVATE
  src
  ${GENERATED}
  ${PICODVI}/include
  ${PICODVI}/apps/colour_terminal
)

# Our sources only: libdvi is an INTERFACE library compiled into this target.
set_source_files_properties(src/demo.c src/diag.c src/keyboard.c src/main.c src/raster.c src/screen.c
  src/splash.c src/term.c src/serial.c src/usbkbd.c src/video.c src/vtparse.c
  PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

target_compile_definitions(dvi_terminal PRIVATE
  DVI_SERIAL_CONFIG=${DVI_SERIAL_CONFIG}
  STARTUP_LOGO=$<BOOL:${STARTUP_LOGO}>
  LOGO_MS=${LOGO_MS}
  STARTUP_DEMO=$<BOOL:${STARTUP_DEMO}>
  VIDEO_DIAG=$<BOOL:${VIDEO_DIAG}>
  USB_KEYBOARD=$<BOOL:${USB_KEYBOARD}>
  SERIAL_FLOW_CONTROL=$<BOOL:${SERIAL_FLOW_CONTROL}>
  DVI_VERTICAL_REPEAT=1
  # The render loop needs very little stack.
  PICO_CORE1_STACK_SIZE=0x200
)

target_link_libraries(dvi_terminal
  pico_stdlib
  pico_multicore
  hardware_uart
  hardware_vreg
  libdvi
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
