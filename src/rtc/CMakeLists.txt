# The real-time clock. dvt_rtc_chips (portable, host-tested): the chip
# drivers (rtc_chip.h; ds3231.c, pcf8523.c), on a register bus they are
# given.
# dvt_rtc (RP2350): the wall clock with the board's chip on I2C (rtc.c),
# kept on the chip's 1 Hz output when the board wires it.
add_library(dvt_rtc_chips STATIC
  ds3231.c
  pcf8523.c
)
target_include_directories(dvt_rtc_chips PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(dvt_rtc_chips PUBLIC dvt_clock)
dvt_compile_options(dvt_rtc_chips)

if(DVT_FIRMWARE)
  add_library(dvt_rtc STATIC rtc.c)
  target_include_directories(dvt_rtc PUBLIC ${CMAKE_CURRENT_LIST_DIR})
  target_link_libraries(dvt_rtc
    PRIVATE
      dvt_board
      dvt_clock
      dvt_rtc_chips
      hardware_gpio_headers
      hardware_i2c_headers
      hardware_irq_headers
      hardware_sync_headers
      hardware_timer_headers
  )
  dvt_compile_options(dvt_rtc)
endif()
