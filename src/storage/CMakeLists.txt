# FatFs's disks (diskio.c: volume 0 the SD card, 1 the USB stick): the SD
# card over SPI (sdcard.c) and USB mass storage (usbmsc.c: TinyUSB's MSC
# host, blocking I/O, which stick is mounted).
add_library(dvt_storage STATIC
  diskio.c
  sdcard.c
  usbmsc.c
)
target_include_directories(dvt_storage PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(dvt_storage
  PUBLIC
    fatfs
  PRIVATE
    dvt_board
    dvt_clock
    dvt_disks
    dvt_platform
    dvt_rtc
    dvt_usb
    hardware_spi_headers
    pico_stdlib_headers
)
dvt_compile_options(dvt_storage)
# Cold code (it runs only while a USB stick comes or goes, or files move):
# small rather than fast, as all of the firmware's code takes SRAM.
set_property(SOURCE usbmsc.c APPEND PROPERTY COMPILE_OPTIONS -Os)

# FatFs calls the disk_* functions diskio.c defines: CMake repeats the two
# archives on the link line.
target_link_libraries(fatfs PUBLIC dvt_storage)
