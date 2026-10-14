# Storage the machine sees, over FatFs: the .DSK images on drives A-H
# (drives.c; drivecfg.c: the image list, DRIVES.CFG, the named sets), the
# built-in images the monitor installs (diskimg.c, generated data), the file
# transfer device's /XFER (xfer.c) and which USB stick is volume 1
# (usbvol.c).
set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
include(diskimg.cmake)

add_library(dvt_disks STATIC
  diskimg.c
  drivecfg.c
  drives.c
  usbvol.c
  xfer.c
  ${DISKIMG_SOURCES}
)
add_dependencies(dvt_disks diskimg_data)
target_include_directories(dvt_disks PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(dvt_disks PUBLIC dvt_common dvt_machine fatfs)
dvt_compile_options(dvt_disks)
# Cold code (it runs only while files move, or a USB stick comes or goes):
# small rather than fast, as all of the firmware's code takes SRAM.
if(DVT_FIRMWARE)
  set_property(SOURCE xfer.c usbvol.c APPEND PROPERTY COMPILE_OPTIONS -Os)
endif()
