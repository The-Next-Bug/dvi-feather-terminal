# The WiFi modem: its start-up (modem.c, bd xir.17.2: reset, the rate, the
# profile and the network) and its calls' DCD and DTR (carrier.c, bd
# xir.17.5), for a Zimodem module on the terminal UART; zmi.c reads its
# firmware image container (bd xir.17.13; zmi_gen.py writes it).
# Portable (test_modem); in WIFI_MODEM firmware builds only, in flash.
add_library(dvt_modem STATIC
  carrier.c
  modem.c
  zmi.c
)
target_include_directories(dvt_modem PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(dvt_modem PUBLIC dvt_common) # crc32
dvt_compile_options(dvt_modem)
