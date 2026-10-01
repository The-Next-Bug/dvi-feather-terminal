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
# above 252 MHz). PicoDVI's apps are built this way too (PICO_COPY_TO_RAM=1).
# See bd dvi-feather-terminal-dmt.10.
# Off by default: needs the two handshake wires (see README, "Flow control").
option(SERIAL_FLOW_CONTROL "RTS/CTS on UART0 (GP3 RTS out, GP2 CTS in)" OFF)
option(COPY_TO_RAM "Run the whole program from SRAM instead of flash" ON)
# A PicoDVI serialiser config from common_dvi_pin_configs.h.
set(DVI_SERIAL_CONFIG pico_sock_cfg CACHE STRING "PicoDVI pin configuration")

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(dvi_feather_terminal C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

if(NOT PICO_PLATFORM MATCHES "^rp2350")
  message(FATAL_ERROR "PICO_BOARD ${PICO_BOARD} is ${PICO_PLATFORM}: only RP2350 boards are supported")
endif()

option(USB_KEYBOARD "USB keyboard on the native USB port (TinyUSB host)" ON)
message(STATUS "USB_KEYBOARD: ${USB_KEYBOARD}")

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
  src/keyname.c
  src/main.c
  src/nvflash.c
  src/raster.c
  src/screen.c
  src/settings.c
  src/setup.c
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
set_source_files_properties(src/demo.c src/diag.c src/keyboard.c src/keyname.c src/main.c src/nvflash.c src/raster.c
  src/screen.c src/settings.c src/setup.c src/splash.c src/term.c src/serial.c src/usbkbd.c src/video.c
  src/vtparse.c
  PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

target_compile_definitions(dvi_terminal PRIVATE
  DVI_SERIAL_CONFIG=${DVI_SERIAL_CONFIG}
  STARTUP_LOGO=$<BOOL:${STARTUP_LOGO}>
  LOGO_MS=${LOGO_MS}
  LOGO_IN_FLASH=$<BOOL:${LOGO_IN_FLASH}>
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
  hardware_flash
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
