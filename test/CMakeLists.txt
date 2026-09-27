# Host unit tests for the portable terminal code (no pico-sdk). Run with
# scripts/test.sh, or:
#   cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test
cmake_minimum_required(VERSION 3.13)
project(dvi_terminal_tests C)
set(CMAKE_C_STANDARD 11)

enable_testing()

set(SRC "${CMAKE_CURRENT_LIST_DIR}/../src")

add_executable(test_screen test_screen.c ${SRC}/screen.c)
target_include_directories(test_screen PRIVATE ${SRC})
target_compile_options(test_screen PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined)
target_link_options(test_screen PRIVATE -fsanitize=address,undefined)
add_test(NAME screen COMMAND test_screen)
