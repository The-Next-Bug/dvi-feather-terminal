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

# unit_test(<name> <sources>...): builds test_<name> from test_<name>.c and
# the given src/ files, and registers it with CTest.
function(unit_test name)
  list(TRANSFORM ARGN PREPEND "${SRC}/")
  add_executable(test_${name} test_${name}.c ${ARGN})
  target_include_directories(test_${name} PRIVATE ${SRC})
  target_compile_options(test_${name} PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined)
  target_link_options(test_${name} PRIVATE -fsanitize=address,undefined)
  add_test(NAME ${name} COMMAND test_${name})
endfunction()

unit_test(screen screen.c)
unit_test(vtparse vtparse.c)
unit_test(term term.c vtparse.c screen.c)
unit_test(raster raster.c screen.c demo.c)
target_sources(test_raster PRIVATE ${FONT_SOURCES})
unit_test(keyboard keyboard.c)
unit_test(keyname keyname.c)
unit_test(settings settings.c crc32.c)
unit_test(setup setup.c settings.c crc32.c screen.c term.c vtparse.c)
unit_test(splash splash.c)
target_sources(test_splash PRIVATE ${LOGO_SOURCES})

# Third-party code: built without our warnings-as-errors (still sanitised).
add_library(fatfs_host STATIC ${FATFS_SOURCES} diskio_ram.c)
target_include_directories(fatfs_host PUBLIC ${FATFS_INCLUDE_DIR} ${CMAKE_CURRENT_LIST_DIR})
target_compile_options(fatfs_host PRIVATE -fsanitize=address,undefined)
add_library(z80pack_host STATIC ${Z80PACK_SOURCES})
target_include_directories(z80pack_host PUBLIC ${Z80PACK_INCLUDE_DIRS})
target_compile_options(z80pack_host PRIVATE -fsanitize=address,undefined)

unit_test(lz lz.c diskimg.c crc32.c)
target_sources(test_lz PRIVATE ${DISKIMG_SOURCES})
add_dependencies(test_lz diskimg_data)
target_link_libraries(test_lz fatfs_host)
unit_test(machine machine.c monitor.c drives.c drivepanel.c screen.c diskimg.c lz.c crc32.c)
target_sources(test_machine PRIVATE ${DISKIMG_SOURCES})
add_dependencies(test_machine diskimg_data)
target_link_libraries(test_machine z80pack_host fatfs_host)
