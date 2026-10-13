# Host unit tests for the portable terminal code (no pico-sdk). Run with
# scripts/test.sh, or:
#   cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test
cmake_minimum_required(VERSION 3.13)
project(dvi_terminal_tests C)
set(CMAKE_C_STANDARD 11)

enable_testing()

set(SRC "${CMAKE_CURRENT_LIST_DIR}/../src")
set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
include(../cmake/fonts.cmake)
include(../cmake/logo.cmake)
include(../cmake/diskimg.cmake)
include(../cmake/z80pack.cmake)
include(../cmake/dvt.cmake)

# The portable libraries, warnings as errors and sanitised like the tests.
set(DVT_C_OPTIONS -Werror -fsanitize=address,undefined)
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

# src/board.h's pin checks against every boards/*.h and some mistakes.
add_test(NAME board COMMAND ${CMAKE_CURRENT_LIST_DIR}/test_board.sh)
set_tests_properties(board PROPERTIES ENVIRONMENT "CC=${CMAKE_C_COMPILER}")

unit_test(screen screen.c)
unit_test(vtparse vtparse.c)
unit_test(term term.c vt100.c vt52.c adm3a.c vtparse.c screen.c palette.c)
unit_test(palette palette.c)
unit_test(raster raster.c screen.c demo.c)
target_sources(test_raster PRIVATE ${FONT_SOURCES})
unit_test(keyboard keyboard.c)
unit_test(blank blank.c)
unit_test(keyname keyname.c)
unit_test(settings settings.c dvt_common)
unit_test(setup setup.c settings.c screen.c dvt_common)
unit_test(splash splash.c)
unit_test(uf2 uf2.c dvt_common)
# Also checks the firmware build/pico2-dvi-sock made, if there is one.
target_compile_definitions(test_uf2 PRIVATE
  FIRMWARE_UF2="${CMAKE_CURRENT_LIST_DIR}/../build/pico2-dvi-sock/dvi_terminal.uf2")
target_sources(test_splash PRIVATE ${LOGO_SOURCES})

# Third-party code: built without our warnings-as-errors (still sanitised).
add_library(fatfs_host STATIC ${FATFS_SOURCES} diskio_ram.c)
target_include_directories(fatfs_host PUBLIC ${FATFS_INCLUDE_DIR} ${CMAKE_CURRENT_LIST_DIR})
target_compile_options(fatfs_host PRIVATE -fsanitize=address,undefined)
add_library(z80pack_host STATIC ${Z80PACK_SOURCES})
target_include_directories(z80pack_host PUBLIC ${Z80PACK_INCLUDE_DIRS})
target_compile_options(z80pack_host PRIVATE -fsanitize=address,undefined)

unit_test(lz diskimg.c dvt_common)
target_sources(test_lz PRIVATE ${DISKIMG_SOURCES})
add_dependencies(test_lz diskimg_data)
target_link_libraries(test_lz fatfs_host)
unit_test(machine machine.c monitor.c drives.c drivecfg.c drivepanel.c fwpanel.c screen.c diskimg.c xfer.c dvt_common
  term.c vt100.c vt52.c adm3a.c vtparse.c palette.c)
target_sources(test_machine PRIVATE machine_host.c ${DISKIMG_SOURCES})
add_dependencies(test_machine diskimg_data)
target_link_libraries(test_machine z80pack_host fatfs_host)
unit_test(xfer xfer.c)
unit_test(usbvol usbvol.c)
target_link_libraries(test_xfer fatfs_host)
# The CPU exercisers (minutes): test_cpu is always built, optimised and
# without the sanitisers (they make it ten times slower), and run only with
# -DCPU_TESTS=ON (scripts/test.sh: CPU_TESTS=1).
option(CPU_TESTS "Run the Z80 instruction exercisers (minutes)" OFF)
add_library(z80pack_fast STATIC ${Z80PACK_SOURCES})
target_include_directories(z80pack_fast PUBLIC ${Z80PACK_INCLUDE_DIRS})
target_compile_options(z80pack_fast PRIVATE -O2)
add_library(fatfs_fast STATIC ${FATFS_SOURCES} diskio_ram.c)
target_include_directories(fatfs_fast PUBLIC ${FATFS_INCLUDE_DIR} ${CMAKE_CURRENT_LIST_DIR})
target_compile_options(fatfs_fast PRIVATE -O2)
set(CPU_TEST_SOURCES machine.c drives.c drivecfg.c diskimg.c xfer.c)
list(TRANSFORM CPU_TEST_SOURCES PREPEND "${SRC}/")
add_executable(test_cpu test_cpu.c machine_host.c ${CPU_TEST_SOURCES} ${DISKIMG_SOURCES})
target_include_directories(test_cpu PRIVATE ${SRC})
dvt_rebuild(test_cpu dvt_common)
target_compile_options(test_cpu PRIVATE -O2 -Wall -Wextra -Werror)
add_dependencies(test_cpu diskimg_data)
target_link_libraries(test_cpu z80pack_fast fatfs_fast)
if(CPU_TESTS)
  add_test(NAME cpu COMMAND test_cpu)
  set_tests_properties(cpu PROPERTIES TIMEOUT 3600)
endif()
