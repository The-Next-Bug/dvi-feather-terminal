cmake_minimum_required(VERSION 3.13)

# For scripts/lint.sh (clang-tidy).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to the submodules and the toolchain installed by
# scripts/setup-toolchain.sh; any of these can be overridden on the command
# line or from the environment.
set(TOOLCHAIN_DIR "${CMAKE_CURRENT_LIST_DIR}/.toolchain")
if(NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
  set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/lib/pico-sdk")
endif()
# TinyUSB: our own pin (lib/tinyusb), not pico-sdk's 0.18, whose RP2350
# host stopped seeing devices for good after a USB stick was pulled: a
# transfer the device never answered was never completed, and there was no
# control-transfer timeout (bd xv0).
if(NOT DEFINED PICO_TINYUSB_PATH AND NOT DEFINED ENV{PICO_TINYUSB_PATH})
  set(PICO_TINYUSB_PATH "${CMAKE_CURRENT_LIST_DIR}/lib/tinyusb")
endif()
# TinyUSB's errors and failed checks (CFG_TUSB_DEBUG 1) go to the USB log
# (usbkbd.c), which can be written to the SD card (USBLOG.TXT).
set(LOG 1)
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
list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/boards"
  "${CMAKE_CURRENT_LIST_DIR}/boards/experimental")
# Boards nobody here has: never run on one, not built by CI
# (boards/experimental/README.md; scripts/build.sh wants EXPERIMENTAL=1).
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/boards/${PICO_BOARD}.h"
    AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/boards/experimental/${PICO_BOARD}.h")
  message(WARNING "PICO_BOARD ${PICO_BOARD} is experimental: untested on hardware")
endif()
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
# Empty: the board header's, pico_board_cmake_set(BOARD_HSTX_MODE, n) (the
# Feathers: 2, they do not run 400 MHz reliably), else 0.
set(HSTX_MODE_HELP "HSTX video mode: empty the board's (else 0), 0 800x600@60 (400 MHz), 1 800x600@56 (360 MHz), 2 800x600@60 reduced blanking (354 MHz)")
set(HSTX_MODE "" CACHE STRING "${HSTX_MODE_HELP}")
# A build directory from before the board's choice holds 0, which would
# now override it: start it over at empty.
get_property(hstx_mode_help CACHE HSTX_MODE PROPERTY HELPSTRING)
if(NOT hstx_mode_help STREQUAL HSTX_MODE_HELP)
  set(HSTX_MODE "" CACHE STRING "${HSTX_MODE_HELP}" FORCE)
endif()
# Z80 CP/M machine (Mode setting): 48 KB memory banks besides the 64 KB of
# bank 0. CP/M 2.2 needs none, CP/M 3 one; MP/M wants as many as it was
# generated for.
set(MACHINE_BANKS 2 CACHE STRING "Z80 machine: number of 48 KB banks (MMU port 64)")
# Or the banks in a QSPI PSRAM on the QMI's second chip select (as many as
# fit, up to 15; none in SRAM): src/platform/extram.c. A board header turns
# it on with pico_board_cmake_set(PSRAM_BANKS, 1)
# (boards/feather-rp2350-hstx-psram.h).
option(PSRAM_BANKS "Z80 machine: banks in the board's PSRAM instead of SRAM" OFF)

# Our workarounds for TinyUSB's RP2350 host bugs (src/usb/usbkbd.c), each
# fixed in the fork lib/tinyusb builds (PRs #4310-#4313, bd 5bc). ON keeps
# a workaround; OFF builds test images that rely on TinyUSB's fix instead
# (hardware validation, bd nxv). USB_RESTART_KEY: H in SET-UP restarts
# the USB host, for testing the deinit path.
option(USB_WA_DEINIT "Free the RP2350 driver's lock ourselves after tusb_deinit (TinyUSB #4310)" ON)
option(USB_WA_DATASEQ "Mask the data sequence error interrupt, restart the host on one (TinyUSB #4312)" ON)
option(USB_WA_POLL_PAUSE "Pause the interrupt endpoint polls around our EPX transfers (TinyUSB #4313)" ON)
option(USB_WA_CONTROL_WAIT "Finish our control transfers before usbkbd_poll returns" ON)
option(USB_RESTART_KEY "Test builds: H in SET-UP restarts the USB host" OFF)

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(dvi_feather_terminal C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

if(NOT PICO_PLATFORM MATCHES "^rp2350")
  message(FATAL_ERROR "PICO_BOARD ${PICO_BOARD} is ${PICO_PLATFORM}: only RP2350 boards are supported")
endif()
if(NOT HSTX_MODE STREQUAL "")
  set(DVT_HSTX_MODE ${HSTX_MODE})
elseif(DEFINED BOARD_HSTX_MODE)
  set(DVT_HSTX_MODE ${BOARD_HSTX_MODE})
else()
  set(DVT_HSTX_MODE 0)
endif()
set(HSTX_MODE_USED ${DVT_HSTX_MODE} CACHE INTERNAL "the video mode built (build-info.txt)")
message(STATUS "HSTX_MODE ${DVT_HSTX_MODE}")
if(PSRAM_BANKS)
  set(MACHINE_BANKS 0) # all of them in the PSRAM: 96 KB of SRAM free
  message(STATUS "Z80 banks in PSRAM (PSRAM_BANKS)")
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
target_compile_definitions(dvt_platform PUBLIC VIDEO_DIAG=$<BOOL:${VIDEO_DIAG}> PSRAM_BANKS=$<BOOL:${PSRAM_BANKS}>)
target_compile_definitions(dvt_video PUBLIC HSTX_MODE=${DVT_HSTX_MODE})
target_compile_definitions(dvt_usb PRIVATE
  USB_WA_DEINIT=$<BOOL:${USB_WA_DEINIT}>
  USB_WA_DATASEQ=$<BOOL:${USB_WA_DATASEQ}>
  USB_WA_POLL_PAUSE=$<BOOL:${USB_WA_POLL_PAUSE}>
  USB_WA_CONTROL_WAIT=$<BOOL:${USB_WA_CONTROL_WAIT}>)

# The programs: the terminal and the HSTX test pattern.
add_subdirectory(apps/terminal)
add_subdirectory(apps/hstx_test)
