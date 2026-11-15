# Print jobs through a Zimodem WiFi modem on a serial line (printjob.c:
# AT+PRINT to an IPP printer; bd wt6.3).
add_library(dvt_printer STATIC
  printjob.c
)
target_include_directories(dvt_printer PUBLIC ${CMAKE_CURRENT_LIST_DIR})
dvt_compile_options(dvt_printer)
