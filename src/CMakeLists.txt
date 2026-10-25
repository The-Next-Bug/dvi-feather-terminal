# The libraries (cmake/dvt.cmake). Portable ones first: the host tests add
# this directory too, without DVT_FIRMWARE.
include(../lib/fatfs.cmake)

add_subdirectory(common)
add_subdirectory(clock)
add_subdirectory(sound)
add_subdirectory(rtc)
add_subdirectory(screen)
add_subdirectory(term)
add_subdirectory(render)
add_subdirectory(input)
add_subdirectory(settings)
add_subdirectory(machine)
add_subdirectory(disks)
add_subdirectory(ui)

if(DVT_FIRMWARE)
  add_subdirectory(board)
  add_subdirectory(platform)
  add_subdirectory(usb)
  add_subdirectory(storage)
  add_subdirectory(video)
endif()
