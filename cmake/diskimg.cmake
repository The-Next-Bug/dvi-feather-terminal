# The CP/M disk images built into the firmware (src/diskimg.h), compressed by
# src/diskimg_gen.py from z80pack's images. Include with GENERATED set to the
# output directory; sets DISKIMG_SOURCES. Shared by the firmware and the host
# tests.

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(Z80PACK_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/z80pack")
set(DISKIMG_GEN "${CMAKE_CURRENT_LIST_DIR}/../src/diskimg_gen.py")
# NAME=file: written to the SD card as /DISKS80/NAME.DSK.
set(DISKIMG_IMAGES
  "CPM22=${Z80PACK_DIR}/picosim/disks/cpm22.dsk"
  "Z80TESTS=${Z80PACK_DIR}/cpmsim/disks/library/z80tests.dsk"
  "EMPTY=${Z80PACK_DIR}/picosim/disks/empty.dsk"
  "CPM31=${Z80PACK_DIR}/picosim/disks/cpm3-1.dsk"
  "CPM32=${Z80PACK_DIR}/picosim/disks/cpm3-2.dsk"
  "CPM3GEN=${Z80PACK_DIR}/picosim/disks/cpm3gen.dsk")
# CPM31 (boot disk) and CPM32 are picosim's banked CP/M 3: mount them on A:
# and B: and boot (bd f8w). CPM3GEN holds its BIOS sources and GENCPM.
set(DISKIMG_SOURCES "${GENERATED}/diskimg_data.c")

# CPM22 gets our CP/M 2.2 system (src/cpm): 62K, eight drives of any of the
# image formats. The boot loader and BIOS are assembled by src/cpm/build.sh
# with z80pack's z80asm, built with the host C compiler.
set(CPM_DIR "${CMAKE_CURRENT_LIST_DIR}/../src/cpm")
set(CPM_OUT "${GENERATED}/cpm")
find_program(HOST_CC NAMES cc gcc clang REQUIRED)
file(GLOB _z80asm_sources "${Z80PACK_DIR}/z80asm/*.[ch]")
add_custom_command(
  OUTPUT ${CPM_OUT}/boot.bin ${CPM_OUT}/bios.bin ${CPM_OUT}/worms.com ${CPM_OUT}/xls.com ${CPM_OUT}/xpip.com ${CPM_OUT}/xera.com ${CPM_OUT}/termtest.com
    ${CPM_OUT}/altscr.com
  COMMAND ${CMAKE_COMMAND} -E env CC=${HOST_CC} ${CPM_DIR}/build.sh ${CPM_OUT}
  DEPENDS ${CPM_DIR}/build.sh ${CPM_DIR}/boot.asm ${CPM_DIR}/bios.asm ${CPM_DIR}/worms.asm
    ${CPM_DIR}/xls.asm ${CPM_DIR}/xpip.asm ${CPM_DIR}/xera.asm ${CPM_DIR}/xfer.inc
    ${CPM_DIR}/termtest.asm ${CPM_DIR}/altscr.asm
    ${_z80asm_sources}
  COMMENT "Assembling the CP/M 2.2 boot loader, BIOS and WORMS"
  VERBATIM)
set(DISKIMG_SYSTEM --system CPM22 ${CPM_OUT}/boot.bin ${CPM_OUT}/bios.bin
  ${Z80PACK_DIR}/picosim/srccpm2/cpm.bin ${CPM_DIR}/ccpbdos.rel 62)
# Our programs on the CPM22 disk; z80pack's XDIR.COM comes off it (it
# refuses drives other than A and B; DIR and STAT cover all eight).
# TERMTEST (the terminal's personalities) and ALTSCR (the alternate
# screen) go with the test programs. Our tools go on CPM32 too (CPM31 is
# nearly full; its PROFILE.SUB puts B: on the search path).
set(DISKIMG_FILES --delete CPM22 XDIR.COM
  --file CPM22 WORMS.COM ${CPM_OUT}/worms.com
  --file CPM22 XLS.COM ${CPM_OUT}/xls.com
  --file CPM22 XPIP.COM ${CPM_OUT}/xpip.com
  --file CPM22 XERA.COM ${CPM_OUT}/xera.com
  --file Z80TESTS TERMTEST.COM ${CPM_OUT}/termtest.com
  --file Z80TESTS ALTSCR.COM ${CPM_OUT}/altscr.com
  --file CPM32 WORMS.COM ${CPM_OUT}/worms.com
  --file CPM32 XLS.COM ${CPM_OUT}/xls.com
  --file CPM32 XPIP.COM ${CPM_OUT}/xpip.com
  --file CPM32 XERA.COM ${CPM_OUT}/xera.com)

set(_diskimg_files)
foreach(_image IN LISTS DISKIMG_IMAGES)
  string(REGEX REPLACE "^[^=]*=" "" _file "${_image}")
  list(APPEND _diskimg_files "${_file}")
endforeach()

add_custom_command(
  OUTPUT ${DISKIMG_SOURCES}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED}
  COMMAND Python3::Interpreter ${DISKIMG_GEN} ${DISKIMG_SOURCES} ${DISKIMG_SYSTEM} ${DISKIMG_FILES}
    ${DISKIMG_IMAGES}
  DEPENDS ${DISKIMG_GEN} ${_diskimg_files} ${CPM_OUT}/boot.bin ${CPM_OUT}/bios.bin ${CPM_OUT}/worms.com
    ${CPM_OUT}/xls.com ${CPM_OUT}/xpip.com ${CPM_OUT}/xera.com ${CPM_OUT}/termtest.com
    ${CPM_OUT}/altscr.com ${Z80PACK_DIR}/picosim/srccpm2/cpm.bin ${CPM_DIR}/ccpbdos.rel
  COMMENT "Compressing the built-in CP/M disk images"
  VERBATIM)

# One target runs the generators; everything that compiles DISKIMG_SOURCES
# depends on it (add_dependencies), so parallel builds do not race them.
add_custom_target(diskimg_data DEPENDS ${DISKIMG_SOURCES})
