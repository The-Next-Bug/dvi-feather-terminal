# The CP/M disk images built into the firmware (diskimg.h), compressed by
# diskimg_gen.py from z80pack's images, for dvt_disks (CMakeLists.txt here:
# GENERATED is the output directory). Sets DISKIMG_SOURCES and the target
# diskimg_data.

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(Z80PACK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../lib/z80pack")
set(DISKIMG_GEN "${CMAKE_CURRENT_LIST_DIR}/diskimg_gen.py")
# NAME=file: written to the SD card as /DISKS80/NAME.DSK.
set(DISKIMG_IMAGES
  "CPM22=${Z80PACK_DIR}/picosim/disks/cpm22.dsk"
  "Z80TESTS=${Z80PACK_DIR}/cpmsim/disks/library/z80tests.dsk"
  "EMPTY=${Z80PACK_DIR}/picosim/disks/empty.dsk"
  "CPM31=${Z80PACK_DIR}/picosim/disks/cpm3-1.dsk"
  "CPM32=${GENERATED}/cpm32.dsk"
  "CPM3GEN=${GENERATED}/cpm3/cpm3gen.dsk"
  "F83=${GENERATED}/f83.dsk"
  "MPM1=${GENERATED}/mpm/mpm1.dsk"
  "MPM2=${GENERATED}/mpm2.dsk")
# CPM31 (boot disk) and CPM32 are picosim's banked CP/M 3: mount them on A:
# and B: and boot (bd f8w). CPM31's CPM3.SYS is ours (below); CPM3GEN is
# picosim's system generation disk with our BIOS sources and what they made.
# CPM32 is picosim's second floppy's files on an 8 MB disk: room for our
# tools, and for temporary files (our BIOS makes B: the temporary drive).
# MPM1 (boot disk) and MPM2 are picosim's MP/M II with our XIOS (bd l1j):
# mount them on A: and B: and boot. MPM1 boots MP/M itself (MPMLDR on its
# system tracks); MPM2, the system generation disk, is on an 8 MB disk, as
# CPM32: room for our tools. Their MPM.SYS is made for this build's banks
# (below).
# F83 is Laxen and Perry's F83 Forth for CP/M-80 (lib/f83, bd xdu): its
# 553 KB of files on a blank 8 MB disk. Its F83.COM is patched by our
# VIEWDRV.BLK so that VIEW reads the source from F83's own drive
# (src/machine/cpm/f83/build.sh, run with cpmrun: below).
set(F83_DIR "${CMAKE_CURRENT_LIST_DIR}/../../lib/f83")
set(F83_FILES -READ.ME README.80 F83-FIXS.TXT KERNEL80.BLK META80.BLK CPU8080.BLK
  EXTEND80.BLK UTILITY.BLK HUFFMAN.BLK CLOCK.BLK EXPAND80.BLK BASIC.BLK)
set(DISKIMG_SOURCES "${GENERATED}/diskimg_data.c")
add_custom_command(
  OUTPUT ${GENERATED}/f83.dsk
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED}
  COMMAND Python3::Interpreter ${DISKIMG_GEN} --convert ${Z80PACK_DIR}/picosim/disks/empty.dsk HD8
    ${GENERATED}/f83.dsk
  DEPENDS ${DISKIMG_GEN} ${Z80PACK_DIR}/picosim/disks/empty.dsk
  COMMENT "Making a blank 8 MB disk for F83"
  VERBATIM)
add_custom_command(
  OUTPUT ${GENERATED}/cpm32.dsk
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED}
  COMMAND Python3::Interpreter ${DISKIMG_GEN} --convert ${Z80PACK_DIR}/picosim/disks/cpm3-2.dsk HD8
    ${GENERATED}/cpm32.dsk
  DEPENDS ${DISKIMG_GEN} ${Z80PACK_DIR}/picosim/disks/cpm3-2.dsk
  COMMENT "Copying CP/M 3's second disk onto an 8 MB disk (CPM32)"
  VERBATIM)

# CPM22 gets our CP/M 2.2 system (src/machine/cpm): 62K, eight drives of
# any of the image formats. The boot loader and BIOS are assembled by
# src/machine/cpm/build.sh with z80pack's z80asm, built with the host C
# compiler.
set(CPM_DIR "${CMAKE_CURRENT_LIST_DIR}/../machine/cpm")
set(CPM_OUT "${GENERATED}/cpm")
find_program(HOST_CC NAMES cc gcc clang REQUIRED)
file(GLOB _z80asm_sources "${Z80PACK_DIR}/z80asm/*.[ch]")
add_custom_command(
  OUTPUT ${CPM_OUT}/z80asm ${CPM_OUT}/boot.bin ${CPM_OUT}/bios.bin ${CPM_OUT}/worms.com ${CPM_OUT}/xls.com ${CPM_OUT}/xpip.com ${CPM_OUT}/xera.com ${CPM_OUT}/termtest.com
    ${CPM_OUT}/altscr.com ${CPM_OUT}/mem.com ${CPM_OUT}/hwinfo.com ${CPM_OUT}/aytest.com ${CPM_OUT}/ayplay.com ${CPM_OUT}/aytrack.com
  COMMAND ${CMAKE_COMMAND} -E env CC=${HOST_CC} ${CPM_DIR}/build.sh ${CPM_OUT}
  DEPENDS ${CPM_DIR}/build.sh ${CPM_DIR}/boot.asm ${CPM_DIR}/bios.asm ${CPM_DIR}/worms.asm
    ${CPM_DIR}/xls.asm ${CPM_DIR}/xpip.asm ${CPM_DIR}/xera.asm ${CPM_DIR}/xfer.inc
    ${CPM_DIR}/termtest.asm ${CPM_DIR}/altscr.asm ${CPM_DIR}/mem.asm ${CPM_DIR}/hwinfo.asm
    ${CPM_DIR}/aytest.asm ${CPM_DIR}/ayplay.asm ${CPM_DIR}/aytrack.asm
    ${_z80asm_sources}
  COMMENT "Assembling the CP/M 2.2 boot loader and BIOS, and our CP/M programs"
  VERBATIM)
# CP/M 3's CPM3.SYS with our banked BIOS (drives A-H, hard disks: bd
# f8w.4), made by DRI's RMAC, LINK and GENCPM running on the Z80 machine on
# the build host (src/machine/cpm/cpm3/build.sh, bd f8w.3).
set(CPM3_DIR "${CPM_DIR}/cpm3")
set(CPM3_OUT "${GENERATED}/cpm3")
file(GLOB _machine_sources "${Z80PACK_DIR}/z80core/*.[ch]" "${Z80PACK_DIR}/iodevices/sd-fdc.[ch]"
  "${CMAKE_CURRENT_LIST_DIR}/../machine/z80pack/*.h" "${CMAKE_CURRENT_LIST_DIR}/../clock/datetime.[ch]")
add_custom_command(
  OUTPUT ${CPM3_OUT}/cpm3.sys ${CPM3_OUT}/vt100dyn.com ${CPM3_OUT}/profile.sub ${CPM3_OUT}/readme.txt
    ${CPM3_OUT}/cpm3gen.dsk ${CPM3_OUT}/cpmrun
  COMMAND ${CMAKE_COMMAND} -E env CC=${HOST_CC} PYTHON=${Python3_EXECUTABLE} ${CPM3_DIR}/build.sh ${CPM3_OUT}
  DEPENDS ${CPM3_DIR}/build.sh ${CPM3_DIR}/cpmrun.c ${CPM3_DIR}/sysgen.run ${CPM3_DIR}/bnkbios3.asm
    ${CPM3_DIR}/gencpm.dat ${CPM3_DIR}/profile.sub ${CPM3_DIR}/readme.txt ${DISKIMG_GEN} ${CMAKE_CURRENT_LIST_DIR}/../machine/machine.c
    ${CMAKE_CURRENT_LIST_DIR}/../machine/machine.h ${_machine_sources}
    ${Z80PACK_DIR}/picosim/disks/cpm3-1.dsk ${Z80PACK_DIR}/picosim/disks/cpm3-2.dsk
    ${Z80PACK_DIR}/picosim/disks/cpm3gen.dsk
  COMMENT "Making CP/M 3's CPM3.SYS (RMAC, LINK, GENCPM on the Z80 machine)"
  VERBATIM)

# F83.COM with VIEWDRV.BLK loaded, saved by F83 under CP/M 2.2 with the
# CP/M 3 step's cpmrun.
set(F83_SRC "${CPM_DIR}/f83")
set(F83_OUT "${GENERATED}/f83")
add_custom_command(
  OUTPUT ${F83_OUT}/f83.com ${F83_OUT}/viewdrv.blk ${F83_OUT}/ay.blk
  COMMAND ${CMAKE_COMMAND} -E make_directory ${F83_OUT}
  COMMAND ${CMAKE_COMMAND} -E env CPMRUN=${CPM3_OUT}/cpmrun PYTHON=${Python3_EXECUTABLE}
    ${F83_SRC}/build.sh ${F83_OUT}
  DEPENDS ${F83_SRC}/build.sh ${F83_SRC}/f83.run ${F83_SRC}/viewdrv.fth ${F83_SRC}/ay.fth
    ${F83_DIR}/F83.COM
    ${CPM3_OUT}/cpmrun ${DISKIMG_GEN} ${Z80PACK_DIR}/picosim/disks/cpm22.dsk
    ${Z80PACK_DIR}/picosim/disks/empty.dsk
  COMMENT "Patching F83.COM's VIEW (F83 on the Z80 machine)"
  VERBATIM)

# MP/M II's MPM.SYS with our XIOS (drives A-H, hard disks: bd l1j), made by
# M80, LINK and GENSYS on the Z80 machine (src/machine/cpm/mpm/build.sh)
# for the banks the machine has: 7 user segments of 48 KB on the PSRAM
# boards (MP/M II's most), MACHINE_BANKS (and bank 0 below the system) on
# the others. The host tests also make the PSRAM boards' (mpm7), which
# test_machine boots on 15 banks.
if(PSRAM_BANKS)
  set(MPM_BANKS 7)
elseif(DEFINED MACHINE_BANKS)
  set(MPM_BANKS ${MACHINE_BANKS})
else()
  set(MPM_BANKS 2) # simmem.h's default, the host tests'
endif()
if(MPM_BANKS GREATER 7)
  set(MPM_BANKS 7)
elseif(MPM_BANKS LESS 1)
  message(FATAL_ERROR "MP/M II needs a bank besides bank 0 (MACHINE_BANKS is ${MACHINE_BANKS})")
endif()
set(MPM_DIR "${CPM_DIR}/mpm")
set(MPM_DEPENDS ${MPM_DIR}/build.sh ${MPM_DIR}/bnkxios.mac ${CPM3_OUT}/cpmrun ${CPM_OUT}/z80asm
  ${DISKIMG_GEN} ${Z80PACK_DIR}/picosim/disks/mpm-1.dsk ${Z80PACK_DIR}/picosim/disks/mpm-2.dsk
  ${Z80PACK_DIR}/picosim/srcmpm/boot.asm ${Z80PACK_DIR}/picosim/srcmpm/mpmldr.bin)
set(MPM_LAYOUTS mpm=${MPM_BANKS}) # output directory=banks
if(NOT DVT_FIRMWARE)
  list(APPEND MPM_LAYOUTS mpm7=7)
endif()
set(MPM_OUTPUTS)
foreach(_layout IN LISTS MPM_LAYOUTS)
  string(REGEX REPLACE "=.*" "" _dir "${_layout}")
  string(REGEX REPLACE ".*=" "" _banks "${_layout}")
  add_custom_command(
    OUTPUT ${GENERATED}/${_dir}/mpm.sys ${GENERATED}/${_dir}/mpm1.dsk ${GENERATED}/${_dir}/mpm2.dsk
    COMMAND ${CMAKE_COMMAND} -E env CPMRUN=${CPM3_OUT}/cpmrun Z80ASM=${CPM_OUT}/z80asm
      PYTHON=${Python3_EXECUTABLE} ${MPM_DIR}/build.sh ${GENERATED}/${_dir} ${_banks}
    DEPENDS ${MPM_DEPENDS}
    COMMENT "Making MP/M II's MPM.SYS for ${_banks} banks (M80, LINK, GENSYS on the Z80 machine)"
    VERBATIM)
  list(APPEND MPM_OUTPUTS ${GENERATED}/${_dir}/mpm1.dsk)
endforeach()
add_custom_command(
  OUTPUT ${GENERATED}/mpm2.dsk
  COMMAND Python3::Interpreter ${DISKIMG_GEN} --convert ${GENERATED}/mpm/mpm2.dsk HD8 ${GENERATED}/mpm2.dsk
  DEPENDS ${DISKIMG_GEN} ${GENERATED}/mpm/mpm2.dsk
  COMMENT "Copying MP/M's second disk onto an 8 MB disk (MPM2)"
  VERBATIM)

# AYPLAY's example tunes (bd 1ls.18.10): our arrangements of public-domain
# melodies, rendered into PSG files.
set(TUNES_OUT "${GENERATED}/tunes")
set(TUNES ${TUNES_OUT}/korobein.psg ${TUNES_OUT}/jacques.psg ${TUNES_OUT}/ode.psg)
add_custom_command(
  OUTPUT ${TUNES}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${TUNES_OUT}
  COMMAND Python3::Interpreter ${CPM_DIR}/tunes/psg_gen.py ${TUNES_OUT}
  DEPENDS ${CPM_DIR}/tunes/psg_gen.py
  COMMENT "Rendering AYPLAY's tunes"
  VERBATIM)

set(DISKIMG_SYSTEM --system CPM22 ${CPM_OUT}/boot.bin ${CPM_OUT}/bios.bin
  ${Z80PACK_DIR}/picosim/srccpm2/cpm.bin ${CPM_DIR}/ccpbdos.rel 62)
# Our programs on the CPM22 disk; z80pack's XDIR.COM comes off it (it
# refuses drives other than A and B; DIR and STAT cover all eight).
# TERMTEST (the terminal's personalities) and ALTSCR (the alternate
# screen) go with the test programs. MEM shows the memory and the banks,
# HWINFO the board and its hardware (also on CPM31: it fits).
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
  --file CPM22 HWINFO.COM ${CPM_OUT}/hwinfo.com
  --file CPM22 AYTEST.COM ${CPM_OUT}/aytest.com
  --file CPM22 AYPLAY.COM ${CPM_OUT}/ayplay.com
  --file CPM22 AYTRACK.COM ${CPM_OUT}/aytrack.com
  --file CPM22 KOROBEIN.PSG ${TUNES_OUT}/korobein.psg
  --file CPM22 JACQUES.PSG ${TUNES_OUT}/jacques.psg
  --file CPM22 ODE.PSG ${TUNES_OUT}/ode.psg
  --file CPM31 HWINFO.COM ${CPM_OUT}/hwinfo.com
  --file CPM31 AYTEST.COM ${CPM_OUT}/aytest.com
  --file Z80TESTS TERMTEST.COM ${CPM_OUT}/termtest.com
  --file Z80TESTS ALTSCR.COM ${CPM_OUT}/altscr.com
  --file CPM32 WORMS.COM ${CPM_OUT}/worms.com
  --file CPM32 XLS.COM ${CPM_OUT}/xls.com
  --file CPM32 XPIP.COM ${CPM_OUT}/xpip.com
  --file CPM32 XERA.COM ${CPM_OUT}/xera.com
  --file CPM32 MEM.COM ${CPM_OUT}/mem.com
  --file CPM32 HWINFO.COM ${CPM_OUT}/hwinfo.com
  --file CPM32 AYTEST.COM ${CPM_OUT}/aytest.com
  --file CPM32 AYPLAY.COM ${CPM_OUT}/ayplay.com
  --file CPM32 AYTRACK.COM ${CPM_OUT}/aytrack.com
  --file CPM32 KOROBEIN.PSG ${TUNES_OUT}/korobein.psg
  --file CPM32 JACQUES.PSG ${TUNES_OUT}/jacques.psg
  --file CPM32 ODE.PSG ${TUNES_OUT}/ode.psg)
# Our tools on MP/M's MPM2 too (MPM1 is nearly full): run them from B: or
# with B: in front (MP/M looks on the current drive, then on A: for
# system files).
foreach(_tool WORMS.COM XLS.COM XPIP.COM XERA.COM MEM.COM HWINFO.COM AYTEST.COM AYPLAY.COM AYTRACK.COM)
  string(TOLOWER ${_tool} _host)
  list(APPEND DISKIMG_FILES --file MPM2 ${_tool} ${CPM_OUT}/${_host})
endforeach()
list(APPEND DISKIMG_FILES --file MPM2 KOROBEIN.PSG ${TUNES_OUT}/korobein.psg
  --file MPM2 JACQUES.PSG ${TUNES_OUT}/jacques.psg --file MPM2 ODE.PSG ${TUNES_OUT}/ode.psg)
set(_f83_paths ${F83_OUT}/f83.com ${F83_OUT}/viewdrv.blk ${F83_OUT}/ay.blk)
list(APPEND DISKIMG_FILES --file F83 F83.COM ${F83_OUT}/f83.com --file F83 VIEWDRV.BLK ${F83_OUT}/viewdrv.blk
  --file F83 AY.BLK ${F83_OUT}/ay.blk)
foreach(_file IN LISTS F83_FILES)
  list(APPEND DISKIMG_FILES --file F83 ${_file} ${F83_DIR}/${_file})
  list(APPEND _f83_paths ${F83_DIR}/${_file})
endforeach()

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
    ${CPM_OUT}/altscr.com ${CPM_OUT}/mem.com ${CPM_OUT}/hwinfo.com ${CPM_OUT}/aytest.com ${CPM_OUT}/ayplay.com ${CPM_OUT}/aytrack.com ${Z80PACK_DIR}/picosim/srccpm2/cpm.bin ${CPM_DIR}/ccpbdos.rel
    ${CPM3_OUT}/cpm3.sys ${CPM3_OUT}/vt100dyn.com ${CPM3_OUT}/profile.sub
    ${CPM3_OUT}/readme.txt ${GENERATED}/cpm32.dsk ${GENERATED}/f83.dsk ${_f83_paths} ${TUNES}
  COMMENT "Compressing the built-in CP/M disk images"
  VERBATIM)

# One target runs the generators; everything that compiles DISKIMG_SOURCES
# depends on it (add_dependencies), so parallel builds do not race them.
add_custom_target(diskimg_data DEPENDS ${DISKIMG_SOURCES} ${MPM_OUTPUTS})
