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
#
# The including project sets DVT_C_OPTIONS (every library, vendored code
# too: the firmware's code generation options, the host tests' sanitisers)
# and DVT_WARNING_OPTIONS (our code only: the host tests' -Werror).

# dvt_compile_options(<target> [THIRD_PARTY]): the options above, and our
# warnings unless THIRD_PARTY (vendored code is built as it comes).
function(dvt_compile_options target)
  target_compile_options(${target} PRIVATE ${DVT_C_OPTIONS})
  if(NOT "THIRD_PARTY" IN_LIST ARGN)
    target_compile_options(${target} PRIVATE -Wall -Wextra ${DVT_WARNING_OPTIONS})
  endif()
endfunction()

# dvt_rebuild(<target> <library>...): compile the libraries' sources into
# <target> with its own options instead of linking the archives (test_cpu:
# optimised, no sanitisers), with the include directories and definitions
# they are built with. Generated sources wait for their library's
# generators.
function(dvt_rebuild target)
  foreach(t ${ARGN})
    get_target_property(dir ${t} SOURCE_DIR)
    get_target_property(srcs ${t} SOURCES)
    foreach(s ${srcs})
      cmake_path(ABSOLUTE_PATH s BASE_DIRECTORY ${dir})
      get_source_file_property(generated ${s} DIRECTORY ${dir} GENERATED)
      if(generated)
        set_source_files_properties(${s} PROPERTIES GENERATED TRUE)
        add_dependencies(${target} ${t})
      endif()
      target_sources(${target} PRIVATE ${s})
    endforeach()
    target_include_directories(${target} PRIVATE
      $<TARGET_PROPERTY:${t},INCLUDE_DIRECTORIES>)
    target_compile_definitions(${target} PRIVATE
      $<TARGET_PROPERTY:${t},COMPILE_DEFINITIONS>)
  endforeach()
endfunction()
