# The libraries (cmake/dvt.cmake). Portable ones first: the host tests add
# this directory too, without DVT_FIRMWARE.
add_subdirectory(common)

if(DVT_FIRMWARE)
endif()
