#!/usr/bin/env bash
# Build the terminal firmware for one target board with the pinned toolchain
# from scripts/setup-toolchain.sh.
#
# Environment overrides:
#   TARGET          board: boards/<TARGET>.h (default: pico2-dvi-sock)
#   EXPERIMENTAL    1 to build a board in boards/experimental/ (untested,
#                   not built by CI: boards/experimental/README.md)
#   BUILD_DIR       output root; the build lands in BUILD_DIR/TARGET (default: build)
#   BUILD_TYPE      CMake build type (default: Release)
#   TOOLCHAIN_DIR   toolchain location (default: <repo>/.toolchain)
#   CMAKE_ARGS      extra arguments for the CMake configure step
#   SDCARD          a mounted SD card: FIRMWARE.UF2 is copied to its root, for
#                   the CP/M monitor's U (update the firmware from the card)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TARGET="${TARGET:-pico2-dvi-sock}"
BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$ROOT/.toolchain}"

# Targets: one per board header, boards/<TARGET>.h (pins, flash, crystal;
# boards/example.h is the template for a new board). Headers for boards
# nobody here has are in boards/experimental/, built only on request.
experimental=no
if [[ ! -f "boards/$TARGET.h" ]]; then
  if [[ -f "boards/experimental/$TARGET.h" ]]; then
    [[ "${EXPERIMENTAL:-}" == 1 ]] || {
      echo "error: boards/experimental/$TARGET.h is experimental (never run on" \
        "the board); build it with EXPERIMENTAL=1" >&2
      exit 1
    }
    experimental=yes
    echo "warning: building experimental board $TARGET: untested on hardware" >&2
  else
    echo "error: unknown TARGET '$TARGET': no boards/$TARGET.h; targets:" \
      $(basename -s .h boards/*.h) "(experimental:" \
      $(basename -s .h boards/experimental/*.h)")" >&2
    exit 1
  fi
fi
pico_board="$TARGET"

cmake="$TOOLCHAIN_DIR/cmake/bin/cmake"
gcc="$TOOLCHAIN_DIR/arm-none-eabi/bin/arm-none-eabi-gcc"
for tool in "$cmake" "$gcc"; do
  [[ -x "$tool" ]] || { echo "error: $tool missing; run scripts/setup-toolchain.sh" >&2; exit 1; }
done
# lib/tinyusb (not pico-sdk's own copy): without it pico-sdk quietly drops
# USB support and the link fails later.
for sub in lib/pico-sdk lib/tinyusb; do
  [[ -f "$sub/.git" || -d "$sub/.git" ]] || {
    echo "error: $sub missing; run: git submodule update --init" >&2
    exit 1
  }
done

out="$BUILD_DIR/$TARGET"
mkdir -p "$out"
"$cmake" -S . -B "$out" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DPICO_BOARD="$pico_board" \
  -DPICO_TOOLCHAIN_PATH="$TOOLCHAIN_DIR/arm-none-eabi" \
  -Dpicotool_DIR="$TOOLCHAIN_DIR/picotool/picotool" \
  ${CMAKE_ARGS:-} \
  | tee "$out/configure.log"
"$cmake" --build "$out" --parallel "$(nproc)" 2>&1 | tee "$out/compile.log"

git_rev() { git -C "$1" rev-parse HEAD 2>/dev/null || echo unknown; }
{
  echo "target=$TARGET"
  echo "pico_board=$pico_board"
  echo "experimental=$experimental"
  echo "dvi_mode=$(sed -n 's/^DVI_MODE_USED:INTERNAL=//p' "$out/CMakeCache.txt")"
  echo "build_type=$BUILD_TYPE"
  echo "machine_banks=$(sed -n 's/^MACHINE_BANKS:STRING=//p' "$out/CMakeCache.txt")"
  # TinyUSB workarounds kept (ON) or built out for testing (bd nxv)
  echo "usb_workarounds=$(for o in DEINIT DATASEQ POLL_PAUSE CONTROL_WAIT; do
    printf '%s=%s ' "$o" "$(sed -n "s/^USB_WA_$o:BOOL=//p" "$out/CMakeCache.txt")"; done)restart_key=$(sed -n 's/^USB_RESTART_KEY:BOOL=//p' "$out/CMakeCache.txt")"
  echo "compiler=$("$gcc" --version | head -1)"
  echo "cmake=$("$cmake" --version | head -1)"
  echo "picotool=$("$TOOLCHAIN_DIR/picotool/picotool/picotool" version | head -1)"
  echo "pico_sdk=$(git -C lib/pico-sdk describe --tags --always 2>/dev/null) $(git_rev lib/pico-sdk)"
  echo "tinyusb=$(git -C lib/tinyusb describe --tags --always 2>/dev/null) $(git_rev lib/tinyusb)"
  echo "git_sha=$(git_rev .)"
  echo "git_describe=$(git describe --always --dirty 2>/dev/null || echo unknown)"
  echo "build_date=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$out/build-info.txt"

# Code that must never read the flash (the firmware update's commit, core 1,
# the clock change, the flash operations) cannot reach cold code or
# constants left in flash.
python3 scripts/check_sram_only.py "$out"

# The name the CP/M monitor's U looks for (8.3: no long names on the card).
cp "$out/dvi_terminal.uf2" "$out/FIRMWARE.UF2"
if [[ -n "${SDCARD:-}" ]]; then
  [[ -d "$SDCARD" ]] || { echo "error: SDCARD=$SDCARD is not a directory" >&2; exit 1; }
  cp "$out/FIRMWARE.UF2" "$SDCARD/FIRMWARE.UF2"
  sync
  echo "copied FIRMWARE.UF2 to $SDCARD"
fi

cat "$out/build-info.txt"
"$TOOLCHAIN_DIR/arm-none-eabi/bin/arm-none-eabi-size" "$out/dvi_terminal.elf"
ls -l "$out/dvi_terminal.uf2"
