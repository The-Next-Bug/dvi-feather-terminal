# The board's pins (board.h: checks the board header, boards/<PICO_BOARD>.h,
# which pico-sdk includes through pico.h). Header only.
add_library(dvt_board INTERFACE)
target_include_directories(dvt_board INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(dvt_board INTERFACE pico_base_headers)
