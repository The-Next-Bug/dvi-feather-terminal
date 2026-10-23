# Dates and times without the C library's localtime (datetime.c) and the
# wall clock kept between reads of the real-time clock (src/rtc/), on the
# chip's 1 Hz output when it has one (wallclock.c).
add_library(dvt_clock STATIC
  datetime.c
  wallclock.c
)
target_include_directories(dvt_clock PUBLIC ${CMAKE_CURRENT_LIST_DIR})
dvt_compile_options(dvt_clock)
