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

# The programs: the terminal and the HSTX test pattern.
add_subdirectory(apps/terminal)
add_subdirectory(apps/hstx_test)
