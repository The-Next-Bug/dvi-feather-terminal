# z80pack's Z80 core (lib/z80pack, MIT licence) for the Z80 machine
# (src/machine.c), configured by the headers in src/z80pack. Sets
# Z80PACK_SOURCES and Z80PACK_INCLUDE_DIRS. Shared by the firmware and the
# host tests. FatFs (lib/fatfs) is here too: FATFS_SOURCES, FATFS_INCLUDE_DIR.

set(Z80PACK_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/z80pack")
set(Z80PACK_SOURCES
  ${Z80PACK_DIR}/z80core/simglb.c
  ${Z80PACK_DIR}/z80core/simcore.c
  ${Z80PACK_DIR}/z80core/simz80.c
  ${Z80PACK_DIR}/z80core/simz80-cb.c
  ${Z80PACK_DIR}/z80core/simz80-dd.c
  ${Z80PACK_DIR}/z80core/simz80-ddcb.c
  ${Z80PACK_DIR}/z80core/simz80-ed.c
  ${Z80PACK_DIR}/z80core/simz80-fd.c
  ${Z80PACK_DIR}/z80core/simz80-fdcb.c
  ${Z80PACK_DIR}/iodevices/sd-fdc.c)
# Ours first: sim.h and friends replace a z80pack machine's.
set(Z80PACK_INCLUDE_DIRS
  ${CMAKE_CURRENT_LIST_DIR}/../src/z80pack
  ${Z80PACK_DIR}/z80core
  ${Z80PACK_DIR}/iodevices)

set(FATFS_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/fatfs")
set(FATFS_SOURCES ${FATFS_DIR}/ff.c)
set(FATFS_INCLUDE_DIR ${FATFS_DIR})
