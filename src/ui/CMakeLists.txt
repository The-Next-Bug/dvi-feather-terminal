# What core 0 draws over the terminal and the keys it takes there: the
# SET-UP panel (setup.c) and its Drives (drivepanel.c), Clock
# (clockpanel.c) and firmware update (fwpanel.c) panels, the CP/M machine's
# monitor (monitor.c) and the startup demo (demo.c). All cold: the firmware
# keeps them in flash.
add_library(dvt_ui STATIC
  clockpanel.c
  demo.c
  drivepanel.c
  fwpanel.c
  monitor.c
  setup.c
)
target_include_directories(dvt_ui PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(dvt_ui PUBLIC dvt_clock dvt_common dvt_disks dvt_machine dvt_screen dvt_settings)
dvt_compile_options(dvt_ui)
