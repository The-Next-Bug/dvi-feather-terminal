# HSTX DVI bring-up test pattern (bd dmt.8.1): hstx_test.uf2 next to the
# terminal, stdio on the board's UART. HSTX_MODE picks the timing
# (src/video/hstx_dvi.h).
add_executable(hstx_test hstx_test.c)
target_compile_options(hstx_test PRIVATE -Wall -Wextra)
# Next to the terminal's files, where CI and scripts/package.sh look.
# (pico_add_extra_outputs writes the UF2 to ARCHIVE_OUTPUT_DIRECTORY.)
set_target_properties(hstx_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_libraries(hstx_test
  dvt_video
  hardware_dma
  hardware_vreg
  pico_stdlib
)
pico_set_binary_type(hstx_test copy_to_ram)
pico_enable_stdio_uart(hstx_test 1)
pico_enable_stdio_usb(hstx_test 0)
pico_add_extra_outputs(hstx_test)
