#!/usr/bin/env bash
# Build the terminal firmware for one target board with the pinned toolchain
# from scripts/setup-toolchain.sh.
#
# Environment overrides:
#   TARGET          board target (default: pico2-dvi-sock; see below)
#   BUILD_DIR       output root; the build lands in BUILD_DIR/TARGET (default: build)
#   BUILD_TYPE      CMake build type (default: Release)
#   TOOLCHAIN_DIR   toolchain location (default: <repo>/.toolchain)
#   SERIAL_FLOW_CONTROL  ON for RTS/CTS on UART0 (default: OFF; see README)
#   CMAKE_ARGS      extra arguments for the CMake configure step
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TARGET="${TARGET:-pico2-dvi-sock}"
BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
SERIAL_FLOW_CONTROL="${SERIAL_FLOW_CONTROL:-OFF}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$ROOT/.toolchain}"

# Targets: pico-sdk board (the DVI pins are the HSTX ones, GP12-19).
case "$TARGET" in
  pico2-dvi-sock) # Raspberry Pi Pico 2 (RP2350) with the Pico DVI Sock
    pico_board=pico2
    ;;
  *)
    echo "error: unknown TARGET '$TARGET'" >&2
    exit 1
    ;;
esac

cmake="$TOOLCHAIN_DIR/cmake/bin/cmake"
gcc="$TOOLCHAIN_DIR/arm-none-eabi/bin/arm-none-eabi-gcc"
for tool in "$cmake" "$gcc"; do
  [[ -x "$tool" ]] || { echo "error: $tool missing; run scripts/setup-toolchain.sh" >&2; exit 1; }
done
for sub in lib/pico-sdk; do
  [[ -f "$sub/.git" || -d "$sub/.git" ]] || {
    echo "error: $sub missing; run: git submodule update --init" >&2
    exit 1
  }
done
# The USB keyboard needs it; without it pico-sdk quietly drops USB support
# and the link fails later.
[[ -f lib/pico-sdk/lib/tinyusb/src/tusb.h ]] || {
  echo "error: TinyUSB missing; run: git -C lib/pico-sdk submodule update --init --depth 1 lib/tinyusb" >&2
  exit 1
}

out="$BUILD_DIR/$TARGET"
mkdir -p "$out"
"$cmake" -S . -B "$out" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DPICO_BOARD="$pico_board" \
  -DPICO_TOOLCHAIN_PATH="$TOOLCHAIN_DIR/arm-none-eabi" \
  -Dpicotool_DIR="$TOOLCHAIN_DIR/picotool/picotool" \
  -DSERIAL_FLOW_CONTROL="$SERIAL_FLOW_CONTROL" \
  ${CMAKE_ARGS:-} \
  | tee "$out/configure.log"
"$cmake" --build "$out" --parallel "$(nproc)" 2>&1 | tee "$out/compile.log"

git_rev() { git -C "$1" rev-parse HEAD 2>/dev/null || echo unknown; }
{
  echo "target=$TARGET"
  echo "pico_board=$pico_board"
  echo "hstx_mode=$(sed -n 's/^HSTX_MODE:STRING=//p' "$out/CMakeCache.txt")"
  echo "build_type=$BUILD_TYPE"
  echo "serial_flow_control=$SERIAL_FLOW_CONTROL"
  echo "usb_keyboard=$(sed -n 's/^USB_KEYBOARD:BOOL=//p' "$out/CMakeCache.txt")"
  echo "machine_banks=$(sed -n 's/^MACHINE_BANKS:STRING=//p' "$out/CMakeCache.txt")"
  echo "compiler=$("$gcc" --version | head -1)"
  echo "cmake=$("$cmake" --version | head -1)"
  echo "picotool=$("$TOOLCHAIN_DIR/picotool/picotool/picotool" version | head -1)"
  echo "pico_sdk=$(git -C lib/pico-sdk describe --tags --always 2>/dev/null) $(git_rev lib/pico-sdk)"
  echo "git_sha=$(git_rev .)"
  echo "git_describe=$(git describe --always --dirty 2>/dev/null || echo unknown)"
  echo "build_date=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$out/build-info.txt"

cat "$out/build-info.txt"
"$TOOLCHAIN_DIR/arm-none-eabi/bin/arm-none-eabi-size" "$out/dvi_terminal.elf"
ls -l "$out/dvi_terminal.uf2"
