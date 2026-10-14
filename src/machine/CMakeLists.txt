# The Z80 machine (machine.c, machine.h): z80pack's Z80 core (lib/z80pack,
# vendored, MIT) configured by our sim*.h in z80pack/, picosim's port map
# and boot ROM. cpm/ holds the machine's CP/M: BIOSes, boot loader and our
# CP/M programs, assembled for the built-in disk images (src/disks).

# z80pack's CPU and FDC, built with our headers first (they replace a
# z80pack machine's). Only machine.c sees its headers: its CPU state is
# globals (A, B, PC, ...).
set(Z80PACK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../lib/z80pack")
add_library(dvt_z80pack STATIC
  ${Z80PACK_DIR}/z80core/simglb.c
  ${Z80PACK_DIR}/z80core/simcore.c
  ${Z80PACK_DIR}/z80core/simz80.c
  ${Z80PACK_DIR}/z80core/simz80-cb.c
  ${Z80PACK_DIR}/z80core/simz80-dd.c
  ${Z80PACK_DIR}/z80core/simz80-ddcb.c
  ${Z80PACK_DIR}/z80core/simz80-ed.c
  ${Z80PACK_DIR}/z80core/simz80-fd.c
  ${Z80PACK_DIR}/z80core/simz80-fdcb.c
  ${Z80PACK_DIR}/iodevices/sd-fdc.c
)
target_include_directories(dvt_z80pack PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/z80pack
  ${Z80PACK_DIR}/z80core
  ${Z80PACK_DIR}/iodevices)
dvt_compile_options(dvt_z80pack THIRD_PARTY)

add_library(dvt_machine STATIC machine.c)
target_include_directories(dvt_machine PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(dvt_machine PRIVATE dvt_z80pack)
dvt_compile_options(dvt_machine)
