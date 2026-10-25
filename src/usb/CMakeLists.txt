# The USB host on the native port: TinyUSB's configuration (tusb_config.h,
# found by TinyUSB's sources, which the program compiles) and the boot
# keyboards (usbkbd.c: reports to keyboard.c, the LEDs, the event log).

# TinyUSB has no headers-only target, and linking tinyusb_host would build
# its sources into the archive: its include directories and definitions
# only.
add_library(dvt_tinyusb_headers INTERFACE)
foreach(t tinyusb_common_base tinyusb_host_base)
  target_include_directories(dvt_tinyusb_headers INTERFACE
    $<TARGET_PROPERTY:${t},INTERFACE_INCLUDE_DIRECTORIES>)
  target_compile_definitions(dvt_tinyusb_headers INTERFACE
    $<TARGET_PROPERTY:${t},INTERFACE_COMPILE_DEFINITIONS>)
endforeach()
target_link_libraries(dvt_tinyusb_headers INTERFACE
  hardware_irq_headers
  hardware_resets_headers
  hardware_structs
  pico_sync_headers
)

add_library(dvt_usb STATIC usbkbd.c)
target_include_directories(dvt_usb PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(dvt_usb
  PUBLIC
    dvt_input
    dvt_tinyusb_headers
  PRIVATE
    dvt_board
    pico_stdlib_headers
)
dvt_compile_options(dvt_usb)
# usbkbd.c's __wrap_rp2usb_deinit: TinyUSB's tuh_deinit frees the driver's
# lock before removing the devices (usbkbd_reset). Not with USB_WA_DEINIT
# OFF (the top CMakeLists.txt), which relies on TinyUSB's fix (#4310).
if(USB_WA_DEINIT)
  target_link_options(dvt_usb INTERFACE -Wl,--wrap=rp2usb_deinit)
endif()
