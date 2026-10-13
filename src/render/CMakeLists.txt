# Screen lines to pixels: raster.c (cells to stored rows, rows to RGB565
# scanlines) with the generated font tables, and the startup logo
# (splash.c and its generated data).
set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
include(fonts.cmake)
include(logo.cmake)

add_library(dvt_render STATIC
  raster.c
  splash.c
  ${FONT_SOURCES}
  ${LOGO_SOURCES}
)
target_include_directories(dvt_render PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(dvt_render PUBLIC dvt_screen)
dvt_warnings(dvt_render)
