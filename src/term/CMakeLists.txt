# The terminal: term.c (the cell-level core and the API, term.h) and the
# personalities (vt100.c with vt52.c, adm3a.c), which share
# term_internal.h; vtparse.c, the DEC escape sequence parser; telnet.c,
# telnet for a host over raw TCP (in front of term_write); unicode.c, what
# UTF-8 characters show as (its tables from unicode_gen.py, checked in).
add_library(dvt_term STATIC
  adm3a.c
  telnet.c
  term.c
  unicode.c
  unicode_table.c
  vt100.c
  vt52.c
  vtparse.c
)
target_include_directories(dvt_term PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(dvt_term PUBLIC dvt_screen)
dvt_compile_options(dvt_term)
