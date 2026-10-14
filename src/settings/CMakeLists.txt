# The settings record (settings.c: fields, validation, the two flash
# sectors' format) and UF2 parsing for the firmware update (uf2.c).
add_library(dvt_settings STATIC
  settings.c
  uf2.c
)
target_include_directories(dvt_settings PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(dvt_settings PUBLIC dvt_common dvt_screen)
dvt_compile_options(dvt_settings)
