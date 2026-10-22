# Dates and times without the C library's localtime (datetime.c), the
# DS3231 real-time clock's register layout (ds3231.c; its I2C is the
# platform's, src/platform/rtc.c) and the wall clock kept between reads,
# on the chip's 1 Hz output when it has one (wallclock.c).
add_library(dvt_clock STATIC
  datetime.c
  ds3231.c
  wallclock.c
)
target_include_directories(dvt_clock PUBLIC ${CMAKE_CURRENT_LIST_DIR})
dvt_compile_options(dvt_clock)
