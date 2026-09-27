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
# A PicoDVI serialiser config from common_dvi_pin_configs.h.
set(DVI_SERIAL_CONFIG adafruit_feather_dvi_cfg CACHE STRING "PicoDVI pin configuration")

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(dvi_feather_terminal C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(PICODVI "${CMAKE_CURRENT_LIST_DIR}/lib/PicoDVI/software")
add_subdirectory(${PICODVI}/libdvi libdvi)

set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(FONT_SRC "${CMAKE_CURRENT_LIST_DIR}/assets/fonts/Uni2-Terminus16.psf.gz")

add_custom_command(
  OUTPUT ${GENERATED}/font_8x16.c
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED}
  COMMAND Python3::Interpreter ${CMAKE_CURRENT_LIST_DIR}/src/font_gen.py
          ${FONT_SRC} ${GENERATED}/font_8x16.c
  DEPENDS ${CMAKE_CURRENT_LIST_DIR}/src/font_gen.py ${FONT_SRC}
  COMMENT "Generating 8x16 font"
  VERBATIM)

# Runs on every build; rewrites term_build.h only when git state changes.
add_custom_target(build_info
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}
          -DOUTPUT=${GENERATED}/term_build.h
          -P ${CMAKE_CURRENT_LIST_DIR}/cmake/build_info.cmake
  BYPRODUCTS ${GENERATED}/term_build.h
  VERBATIM)

add_executable(dvi_terminal
  src/main.c
  src/screen.c
  src/term.c
  src/uart_rx.c
  src/video.c
  ${GENERATED}/font_8x16.c
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
set_source_files_properties(src/main.c src/screen.c src/term.c src/uart_rx.c src/video.c
  PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

target_compile_definitions(dvi_terminal PRIVATE
  DVI_SERIAL_CONFIG=${DVI_SERIAL_CONFIG}
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

# UART0 belongs to the terminal (uart_rx.c): no stdio on it or on USB.
pico_enable_stdio_uart(dvi_terminal 0)
pico_enable_stdio_usb(dvi_terminal 0)
pico_add_extra_outputs(dvi_terminal)
