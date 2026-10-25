# The audio engine's portable part: the AY-3-8910 (ay8910.c, bd 1ls.18.3)
# and the mix of it and the bell (mixer.c, bd 1ls.18.2). They run in the
# engine's refill interrupt, so they stay in SRAM (not in ld/'s cold
# objects).
add_library(dvt_sound STATIC
  ay8910.c
  mixer.c
)
target_include_directories(dvt_sound PUBLIC ${CMAKE_CURRENT_LIST_DIR})
dvt_compile_options(dvt_sound)
