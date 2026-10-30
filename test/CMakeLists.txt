# Host unit tests for the portable terminal code (no pico-sdk). Run with
# scripts/test.sh, or:
#   cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test
cmake_minimum_required(VERSION 3.13)
# For scripts/lint.sh (clang-tidy).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(dvi_terminal_tests C)
set(CMAKE_C_STANDARD 11)

enable_testing()

set(SRC "${CMAKE_CURRENT_LIST_DIR}/../src")
include(../cmake/dvt.cmake)

# The portable libraries, sanitized like the tests; ours with warnings as
# errors (FatFs and z80pack built as they come).
set(DVT_C_OPTIONS -fsanitize=address,undefined)
set(DVT_WARNING_OPTIONS -Werror)
add_subdirectory(../src src)

# unit_test(<name> <sources or libraries>...): builds test_<name> from
# test_<name>.c, the given src/ files and dvt_* libraries, and registers it
# with CTest.
function(unit_test name)
  set(srcs "")
  set(libs "")
  foreach(a ${ARGN})
    if(TARGET ${a})
      list(APPEND libs ${a})
    else()
      list(APPEND srcs "${SRC}/${a}")
    endif()
  endforeach()
  add_executable(test_${name} test_${name}.c ${srcs})
  target_link_libraries(test_${name} ${libs})
  target_include_directories(test_${name} PRIVATE ${SRC})
  target_compile_options(test_${name} PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined)
  target_link_options(test_${name} PRIVATE -fsanitize=address,undefined)
  add_test(NAME ${name} COMMAND test_${name})
endfunction()

# src/board/board.h's pin checks against every boards/*.h and some mistakes.
add_test(NAME board COMMAND ${CMAKE_CURRENT_LIST_DIR}/test_board.sh)
set_tests_properties(board PROPERTIES ENVIRONMENT "CC=${CMAKE_C_COMPILER}")

unit_test(screen dvt_screen)
unit_test(vtparse dvt_term)
unit_test(term dvt_term)
unit_test(palette dvt_screen)
unit_test(raster dvt_render dvt_ui)
# The user guide's screenshots and settings tables (docs/guide/gen/docgen.c,
# run by scripts/docs.sh; bd 1ay): built with the tests, not one of them.
add_executable(docgen ${CMAKE_CURRENT_LIST_DIR}/../docs/guide/gen/docgen.c)
target_link_libraries(docgen dvt_ui dvt_term dvt_render dvt_settings dvt_clock dvt_screen)
target_include_directories(docgen PRIVATE ${SRC})
target_compile_options(docgen PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined)
target_link_options(docgen PRIVATE -fsanitize=address,undefined)
unit_test(keyboard dvt_input)
unit_test(blank dvt_input)
unit_test(keyname dvt_input)
unit_test(settings dvt_settings)
unit_test(setup dvt_ui)
unit_test(splash dvt_render)
unit_test(clock dvt_clock)
unit_test(ay dvt_sound)
unit_test(rtc dvt_rtc_chips)
unit_test(xonxoff dvt_common)
unit_test(uf2 dvt_settings)
# Also checks the firmware build/pico2-dvi-sock made, if there is one.
target_compile_definitions(test_uf2 PRIVATE
  FIRMWARE_UF2="${CMAKE_CURRENT_LIST_DIR}/../build/pico2-dvi-sock/dvi_terminal.uf2")

# FatFs on RAM volumes (diskio_ram.c, the disk_* functions FatFs calls).
unit_test(lz dvt_disks)
target_sources(test_lz PRIVATE diskio_ram.c)
unit_test(machine dvt_ui dvt_term dvt_clock dvt_sound)
target_sources(test_machine PRIVATE machine_host.c diskio_ram.c)
# AYPLAY's tunes as built for the disks, to compare its writes with.
target_compile_definitions(test_machine PRIVATE TUNES_DIR="${CMAKE_BINARY_DIR}/src/disks/generated/tunes"
  MPM7_DIR="${CMAKE_BINARY_DIR}/src/disks/generated/mpm7")
unit_test(xfer dvt_disks)
target_sources(test_xfer PRIVATE diskio_ram.c)
unit_test(usbvol dvt_disks)
# Its own sparse disk (any card size) as drive 0.
unit_test(sdformat dvt_disks)
# The CPU exercisers (minutes): test_cpu is always built, optimized and
# without the sanitizers (they make it ten times slower), and run only with
# -DCPU_TESTS=ON (scripts/test.sh: CPU_TESTS=1). Its own copies of the
# libraries: ours with the test's warnings, the vendored ones as they come.
option(CPU_TESTS "Run the Z80 instruction exercisers (minutes)" OFF)
add_library(cpu_vendored STATIC)
dvt_rebuild(cpu_vendored dvt_z80pack fatfs)
target_compile_options(cpu_vendored PRIVATE -O2)
add_executable(test_cpu test_cpu.c machine_host.c diskio_ram.c)
dvt_rebuild(test_cpu dvt_common dvt_clock dvt_sound dvt_machine dvt_disks)
target_compile_options(test_cpu PRIVATE -O2 -Wall -Wextra -Werror)
target_link_libraries(test_cpu cpu_vendored)
if(CPU_TESTS)
  add_test(NAME cpu COMMAND test_cpu)
  set_tests_properties(cpu PROPERTIES TIMEOUT 3600)
endif()
