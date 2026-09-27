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
