# Helpers for the libraries under src/ (bd gr1), shared by the firmware and
# the host tests.
#
# A library is a directory src/<name>/ with a CMakeLists.txt making the
# static library dvt_<name>: its public headers next to its sources (the
# directory is its PUBLIC include path), headers only its own sources share
# named <name>_internal.h. A library is either portable (no pico-sdk
# headers: the host tests build it) or RP2350 (added only with
# DVT_FIRMWARE; links the SDK's *_headers targets, the program links the
# SDK libraries themselves, so no SDK source is compiled into an archive).

# Compile options for every library source: the warnings, plus whatever
# the including project adds in DVT_C_OPTIONS (the host tests: -Werror and
# the sanitisers).
function(dvt_warnings target)
  target_compile_options(${target} PRIVATE -Wall -Wextra ${DVT_C_OPTIONS})
endfunction()

# dvt_rebuild(<target> <library>...): compile the libraries' sources into
# <target> with its own options instead of linking the archives (test_cpu:
# optimised, no sanitisers), with their include directories.
function(dvt_rebuild target)
  foreach(t ${ARGN})
    get_target_property(dir ${t} SOURCE_DIR)
    get_target_property(srcs ${t} SOURCES)
    foreach(s ${srcs})
      cmake_path(ABSOLUTE_PATH s BASE_DIRECTORY ${dir})
      target_sources(${target} PRIVATE ${s})
    endforeach()
    target_include_directories(${target} PRIVATE
      $<TARGET_PROPERTY:${t},INTERFACE_INCLUDE_DIRECTORIES>)
  endforeach()
endfunction()
