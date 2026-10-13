# The screen's cells (glyph codes: font.h) and the xterm-256 palette.
add_library(dvt_screen STATIC
  palette.c
  screen.c
)
target_include_directories(dvt_screen PUBLIC ${CMAKE_CURRENT_LIST_DIR})
dvt_warnings(dvt_screen)
