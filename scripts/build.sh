#!/usr/bin/env bash
# Build the terminal firmware for one target board with the pinned toolchain
# from scripts/setup-toolchain.sh.
#
# Environment overrides:
#   TARGET          board target (default: feather-rp2040-dvi; see below)
#   BUILD_DIR       output root; the build lands in BUILD_DIR/TARGET (default: build)
#   BUILD_TYPE      CMake build type (default: Release)
#   TOOLCHAIN_DIR   toolchain location (default: <repo>/.toolchain)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TARGET="${TARGET:-feather-rp2040-dvi}"
BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$ROOT/.toolchain}"

# Targets: pico-sdk board and PicoDVI pin configuration.
case "$TARGET" in
  feather-rp2040-dvi)
    pico_board=adafruit_feather_rp2040
    dvi_config=adafruit_feather_dvi_cfg
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
for sub in lib/pico-sdk lib/PicoDVI; do
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
  -DDVI_SERIAL_CONFIG="$dvi_config" \
  -DPICO_TOOLCHAIN_PATH="$TOOLCHAIN_DIR/arm-none-eabi" \
  -Dpicotool_DIR="$TOOLCHAIN_DIR/picotool/picotool" \
  | tee "$out/configure.log"
"$cmake" --build "$out" --parallel "$(nproc)" 2>&1 | tee "$out/compile.log"

git_rev() { git -C "$1" rev-parse HEAD 2>/dev/null || echo unknown; }
{
  echo "target=$TARGET"
  echo "pico_board=$pico_board"
  echo "dvi_serial_config=$dvi_config"
  echo "build_type=$BUILD_TYPE"
  echo "compiler=$("$gcc" --version | head -1)"
  echo "cmake=$("$cmake" --version | head -1)"
  echo "picotool=$("$TOOLCHAIN_DIR/picotool/picotool/picotool" version | head -1)"
  echo "pico_sdk=$(git -C lib/pico-sdk describe --tags --always 2>/dev/null) $(git_rev lib/pico-sdk)"
  echo "picodvi=$(git_rev lib/PicoDVI)"
  echo "git_sha=$(git_rev .)"
  echo "git_describe=$(git describe --always --dirty 2>/dev/null || echo unknown)"
  echo "build_date=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$out/build-info.txt"

cat "$out/build-info.txt"
"$TOOLCHAIN_DIR/arm-none-eabi/bin/arm-none-eabi-size" "$out/dvi_terminal.elf"
ls -l "$out/dvi_terminal.uf2"
