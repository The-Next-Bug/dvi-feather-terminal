# The libraries (cmake/dvt.cmake). Portable ones first: the host tests add
# this directory too, without DVT_FIRMWARE.
add_subdirectory(common)
add_subdirectory(screen)
add_subdirectory(term)
add_subdirectory(render)
add_subdirectory(input)

if(DVT_FIRMWARE)
endif()
