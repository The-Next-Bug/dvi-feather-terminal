# Small shared pieces: CRC-32, the LZ4-format decoder, the 4 KB scratch
# buffer (one holder at a time).
add_library(dvt_common STATIC
  crc32.c
  lz.c
  scratch.c
)
target_include_directories(dvt_common PUBLIC ${CMAKE_CURRENT_LIST_DIR})
dvt_warnings(dvt_common)
