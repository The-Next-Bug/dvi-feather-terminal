# ChaN's FatFs (lib/fatfs, vendored; ffconf.h there), for the libraries
# (src/CMakeLists.txt includes this). Its disk_* functions come from the
# program: the firmware's src/diskio.c, the host tests' test/diskio_ram.c.
add_library(fatfs STATIC ${CMAKE_CURRENT_LIST_DIR}/fatfs/ff.c)
target_include_directories(fatfs PUBLIC ${CMAKE_CURRENT_LIST_DIR}/fatfs)
dvt_compile_options(fatfs THIRD_PARTY)
