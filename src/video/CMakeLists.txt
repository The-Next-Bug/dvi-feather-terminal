# DVI out: the HSTX driver (hstx_dvi.c: timing, TMDS, DMA, the blanking
# command lists) and core 1's scanline composer (video.c: rasterizes dirty
# lines on core 0, composes scanlines on core 1 from SRAM only).
add_library(dvt_video STATIC
  hstx_dvi.c
  video.c
)
target_include_directories(dvt_video PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(dvt_video
  PUBLIC
    dvt_render
    dvt_screen
  PRIVATE
    dvt_board
    dvt_platform
    hardware_clocks_headers
    hardware_dma_headers
    hardware_irq_headers
    hardware_pll_headers
    hardware_structs
    hardware_sync_headers
    hardware_vreg_headers
    pico_multicore_headers
    pico_stdlib_headers
)
dvt_compile_options(dvt_video)
