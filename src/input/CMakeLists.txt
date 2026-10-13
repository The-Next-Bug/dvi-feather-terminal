# Keys: HID boot keyboard reports to VT100 key codes (keyboard.c), key
# bytes to names for the status line (keyname.c), the idle timer that
# blanks the screen (blank.c).
add_library(dvt_input STATIC
  blank.c
  keyboard.c
  keyname.c
)
target_include_directories(dvt_input PUBLIC ${CMAKE_CURRENT_LIST_DIR})
dvt_warnings(dvt_input)
