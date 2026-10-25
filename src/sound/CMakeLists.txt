# Sound sources rendered for the audio engine: the AY-3-8910 (ay8910.c,
# bd 1ls.18.3). Portable; its render runs in the engine's refill interrupt,
# so it stays in SRAM (not in ld/'s cold objects).
add_library(dvt_sound STATIC
  ay8910.c
)
target_include_directories(dvt_sound PUBLIC ${CMAKE_CURRENT_LIST_DIR})
dvt_compile_options(dvt_sound)
