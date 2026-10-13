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
  "CPM32=${GENERATED}/cpm32.dsk"
  "CPM3GEN=${GENERATED}/cpm3/cpm3gen.dsk")
# CPM31 (boot disk) and CPM32 are picosim's banked CP/M 3: mount them on A:
# and B: and boot (bd f8w). CPM31's CPM3.SYS is ours (below); CPM3GEN is
# picosim's system generation disk with our BIOS sources and what they made.
# CPM32 is picosim's second floppy's files on an 8 MB disk: room for our
# tools, and for temporary files (our BIOS makes B: the temporary drive).
set(DISKIMG_SOURCES "${GENERATED}/diskimg_data.c")
add_custom_command(
  OUTPUT ${GENERATED}/cpm32.dsk
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED}
  COMMAND Python3::Interpreter ${DISKIMG_GEN} --convert ${Z80PACK_DIR}/picosim/disks/cpm3-2.dsk HD8
    ${GENERATED}/cpm32.dsk
  DEPENDS ${DISKIMG_GEN} ${Z80PACK_DIR}/picosim/disks/cpm3-2.dsk
  COMMENT "Copying CP/M 3's second disk onto an 8 MB disk (CPM32)"
  VERBATIM)

# CPM22 gets our CP/M 2.2 system (src/cpm): 62K, eight drives of any of the
# image formats. The boot loader and BIOS are assembled by src/cpm/build.sh
# with z80pack's z80asm, built with the host C compiler.
set(CPM_DIR "${CMAKE_CURRENT_LIST_DIR}/../src/cpm")
set(CPM_OUT "${GENERATED}/cpm")
find_program(HOST_CC NAMES cc gcc clang REQUIRED)
file(GLOB _z80asm_sources "${Z80PACK_DIR}/z80asm/*.[ch]")
add_custom_command(
  OUTPUT ${CPM_OUT}/boot.bin ${CPM_OUT}/bios.bin ${CPM_OUT}/worms.com ${CPM_OUT}/xls.com ${CPM_OUT}/xpip.com ${CPM_OUT}/xera.com ${CPM_OUT}/termtest.com
    ${CPM_OUT}/altscr.com ${CPM_OUT}/mem.com
  COMMAND ${CMAKE_COMMAND} -E env CC=${HOST_CC} ${CPM_DIR}/build.sh ${CPM_OUT}
  DEPENDS ${CPM_DIR}/build.sh ${CPM_DIR}/boot.asm ${CPM_DIR}/bios.asm ${CPM_DIR}/worms.asm
    ${CPM_DIR}/xls.asm ${CPM_DIR}/xpip.asm ${CPM_DIR}/xera.asm ${CPM_DIR}/xfer.inc
    ${CPM_DIR}/termtest.asm ${CPM_DIR}/altscr.asm ${CPM_DIR}/mem.asm
    ${_z80asm_sources}
  COMMENT "Assembling the CP/M 2.2 boot loader and BIOS, and our CP/M programs"
  VERBATIM)
# CP/M 3's CPM3.SYS with our banked BIOS (drives A-H, hard disks: bd
# f8w.4), made by DRI's RMAC, LINK and GENCPM running on the Z80 machine on
# the build host (src/cpm/cpm3/build.sh, bd f8w.3).
set(CPM3_DIR "${CPM_DIR}/cpm3")
set(CPM3_OUT "${GENERATED}/cpm3")
file(GLOB _machine_sources "${Z80PACK_DIR}/z80core/*.[ch]" "${Z80PACK_DIR}/iodevices/sd-fdc.[ch]"
  "${CMAKE_CURRENT_LIST_DIR}/../src/z80pack/*.h")
add_custom_command(
  OUTPUT ${CPM3_OUT}/cpm3.sys ${CPM3_OUT}/vt100dyn.com ${CPM3_OUT}/profile.sub ${CPM3_OUT}/readme.txt
    ${CPM3_OUT}/cpm3gen.dsk
  COMMAND ${CMAKE_COMMAND} -E env CC=${HOST_CC} PYTHON=${Python3_EXECUTABLE} ${CPM3_DIR}/build.sh ${CPM3_OUT}
  DEPENDS ${CPM3_DIR}/build.sh ${CPM3_DIR}/cpmrun.c ${CPM3_DIR}/sysgen.run ${CPM3_DIR}/bnkbios3.asm
    ${CPM3_DIR}/gencpm.dat ${CPM3_DIR}/profile.sub ${CPM3_DIR}/readme.txt ${DISKIMG_GEN} ${CMAKE_CURRENT_LIST_DIR}/../src/machine.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/machine.h ${_machine_sources}
    ${Z80PACK_DIR}/picosim/disks/cpm3-1.dsk ${Z80PACK_DIR}/picosim/disks/cpm3-2.dsk
    ${Z80PACK_DIR}/picosim/disks/cpm3gen.dsk
  COMMENT "Making CP/M 3's CPM3.SYS (RMAC, LINK, GENCPM on the Z80 machine)"
  VERBATIM)

set(DISKIMG_SYSTEM --system CPM22 ${CPM_OUT}/boot.bin ${CPM_OUT}/bios.bin
  ${Z80PACK_DIR}/picosim/srccpm2/cpm.bin ${CPM_DIR}/ccpbdos.rel 62)
# Our programs on the CPM22 disk; z80pack's XDIR.COM comes off it (it
# refuses drives other than A and B; DIR and STAT cover all eight).
# TERMTEST (the terminal's personalities) and ALTSCR (the alternate
# screen) go with the test programs. MEM shows the memory and the banks.
# Our tools go on CPM32 too (CPM31 is nearly full; B: is on CP/M 3's
# search path), and README.TXT, on the defaults, on CPM31.
set(DISKIMG_FILES --delete CPM22 XDIR.COM
  --delete CPM31 CPM3.SYS
  --delete CPM31 VT100DYN.COM
  --delete CPM31 PROFILE.SUB
  --file CPM31 CPM3.SYS ${CPM3_OUT}/cpm3.sys
  --file CPM31 VT100DYN.COM ${CPM3_OUT}/vt100dyn.com
  --file CPM31 PROFILE.SUB ${CPM3_OUT}/profile.sub
  --file CPM31 README.TXT ${CPM3_OUT}/readme.txt
  --file CPM22 WORMS.COM ${CPM_OUT}/worms.com
  --file CPM22 XLS.COM ${CPM_OUT}/xls.com
  --file CPM22 XPIP.COM ${CPM_OUT}/xpip.com
  --file CPM22 XERA.COM ${CPM_OUT}/xera.com
  --file CPM22 MEM.COM ${CPM_OUT}/mem.com
  --file Z80TESTS TERMTEST.COM ${CPM_OUT}/termtest.com
  --file Z80TESTS ALTSCR.COM ${CPM_OUT}/altscr.com
  --file CPM32 WORMS.COM ${CPM_OUT}/worms.com
  --file CPM32 XLS.COM ${CPM_OUT}/xls.com
  --file CPM32 XPIP.COM ${CPM_OUT}/xpip.com
  --file CPM32 XERA.COM ${CPM_OUT}/xera.com
  --file CPM32 MEM.COM ${CPM_OUT}/mem.com)

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
    ${CPM_OUT}/altscr.com ${CPM_OUT}/mem.com ${Z80PACK_DIR}/picosim/srccpm2/cpm.bin ${CPM_DIR}/ccpbdos.rel
    ${CPM3_OUT}/cpm3.sys ${CPM3_OUT}/vt100dyn.com ${CPM3_OUT}/profile.sub
    ${CPM3_OUT}/readme.txt ${GENERATED}/cpm32.dsk
  COMMENT "Compressing the built-in CP/M disk images"
  VERBATIM)

# One target runs the generators; everything that compiles DISKIMG_SOURCES
# depends on it (add_dependencies), so parallel builds do not race them.
add_custom_target(diskimg_data DEPENDS ${DISKIMG_SOURCES})
