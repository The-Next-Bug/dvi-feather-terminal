# The libraries (cmake/dvt.cmake). Portable ones first: the host tests add
# this directory too, without DVT_FIRMWARE.
include(../lib/fatfs.cmake)

add_subdirectory(common)
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
endif()
