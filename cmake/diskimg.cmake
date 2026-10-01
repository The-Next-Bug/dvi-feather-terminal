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
  "EMPTY=${Z80PACK_DIR}/picosim/disks/empty.dsk")
set(DISKIMG_SOURCES "${GENERATED}/diskimg_data.c")

# CPM22 gets our CP/M 2.2 system (src/cpm): 62K, eight drives of any of the
# image formats. The boot loader and BIOS are assembled by src/cpm/build.sh
# with z80pack's z80asm, built with the host C compiler.
set(CPM_DIR "${CMAKE_CURRENT_LIST_DIR}/../src/cpm")
set(CPM_OUT "${GENERATED}/cpm")
find_program(HOST_CC NAMES cc gcc clang REQUIRED)
file(GLOB _z80asm_sources "${Z80PACK_DIR}/z80asm/*.[ch]")
add_custom_command(
  OUTPUT ${CPM_OUT}/boot.bin ${CPM_OUT}/bios.bin ${CPM_OUT}/worms.com
  COMMAND ${CMAKE_COMMAND} -E env CC=${HOST_CC} ${CPM_DIR}/build.sh ${CPM_OUT}
  DEPENDS ${CPM_DIR}/build.sh ${CPM_DIR}/boot.asm ${CPM_DIR}/bios.asm ${CPM_DIR}/worms.asm
    ${_z80asm_sources}
  COMMENT "Assembling the CP/M 2.2 boot loader, BIOS and WORMS"
  VERBATIM)
set(DISKIMG_SYSTEM --system CPM22 ${CPM_OUT}/boot.bin ${CPM_OUT}/bios.bin
  ${Z80PACK_DIR}/picosim/srccpm2/cpm.bin ${CPM_DIR}/ccpbdos.rel 62)
# Our programs on the CPM22 disk.
set(DISKIMG_FILES --file CPM22 WORMS.COM ${CPM_OUT}/worms.com)

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
    ${Z80PACK_DIR}/picosim/srccpm2/cpm.bin ${CPM_DIR}/ccpbdos.rel
  COMMENT "Compressing the built-in CP/M disk images"
  VERBATIM)

# One target runs the generators; everything that compiles DISKIMG_SOURCES
# depends on it (add_dependencies), so parallel builds do not race them.
add_custom_target(diskimg_data DEPENDS ${DISKIMG_SOURCES})
