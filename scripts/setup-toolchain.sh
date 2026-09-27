#!/usr/bin/env bash
# Install the pinned build toolchain into TOOLCHAIN_DIR. Used by CI and local
# builds; nothing is installed system-wide.
#
# Usage: scripts/setup-toolchain.sh [component...]
#   components: cmake arm picotool (default: all). The host-test job only
#   needs cmake.
#
# Versions come from toolchain.env; any of its variables can be overridden
# from the environment (e.g. ARM_GNU_VERSION=... ARM_GNU_SHA256=...).
#
# Other environment overrides:
#   TOOLCHAIN_DIR   install location (default: <repo>/.toolchain)
#
# Each component is skipped when its install records the pinned version.
# Downloads are kept, verified, in TOOLCHAIN_DIR/downloads: CI caches that
# (about 200 MB) plus the small picotool build, rather than the ~1.2 GB of
# unpacked toolchain, and unpacks it in each job.
#
# Host requirements: curl, tar, xz, sha256sum, git, a host C/C++ compiler and
# make (picotool is built from source).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Load toolchain.env without clobbering values already set in the environment.
while IFS='=' read -r key value; do
  [[ "$key" =~ ^[A-Z_][A-Z0-9_]*$ ]] || continue
  [[ -n "${!key:-}" ]] || printf -v "$key" '%s' "$value"
done < "$ROOT/toolchain.env"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$ROOT/.toolchain}"

components=("$@")
[[ ${#components[@]} -gt 0 ]] || components=(cmake arm picotool)

mkdir -p "$TOOLCHAIN_DIR"

# fetch URL SHA256: print the path of the verified download, reusing a
# previous one.
fetch() {
  local dest="$TOOLCHAIN_DIR/downloads/${1##*/}"
  mkdir -p "$TOOLCHAIN_DIR/downloads"
  if ! { [[ -f "$dest" ]] && echo "$2  $dest" | sha256sum -c --status - >/dev/null 2>&1; }; then
    curl -sSfL -o "$dest.part" "$1"
    echo "$2  $dest.part" | sha256sum -c - >&2
    mv "$dest.part" "$dest"
  fi
  echo "$dest"
}

# installed DIR VERSION: true if DIR holds an install of VERSION.
installed() {
  [[ "$(cat "$1/.version" 2>/dev/null)" == "$2" ]]
}

install_cmake() {
  local dir="$TOOLCHAIN_DIR/cmake"
  if ! installed "$dir" "$CMAKE_VERSION"; then
    local tarball
    tarball="$(fetch "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" \
      "$CMAKE_SHA256")"
    rm -rf "$dir" && mkdir -p "$dir"
    tar -xzf "$tarball" -C "$dir" --strip-components=1
    echo "$CMAKE_VERSION" > "$dir/.version"
  fi
  "$dir/bin/cmake" --version | head -1
}

install_arm() {
  local dir="$TOOLCHAIN_DIR/arm-none-eabi"
  if ! installed "$dir" "$ARM_GNU_VERSION"; then
    local tarball
    tarball="$(fetch "https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_GNU_VERSION}/binrel/arm-gnu-toolchain-${ARM_GNU_VERSION}-x86_64-arm-none-eabi.tar.xz" \
      "$ARM_GNU_SHA256")"
    rm -rf "$dir" && mkdir -p "$dir"
    tar -xJf "$tarball" -C "$dir" --strip-components=1
    echo "$ARM_GNU_VERSION" > "$dir/.version"
  fi
  "$dir/bin/arm-none-eabi-gcc" --version | head -1
}

# picotool is needed by pico-sdk 2.x to make UF2s. Without it the SDK would
# download and build it during every fresh configure; building it once here
# keeps builds offline and pinned. USB features (libusb) are not needed.
install_picotool() {
  local dir="$TOOLCHAIN_DIR/picotool"
  local cmake="$TOOLCHAIN_DIR/cmake/bin/cmake"
  [[ -x "$cmake" ]] || { echo "error: picotool needs cmake; install it first" >&2; exit 1; }
  if ! installed "$dir" "$PICOTOOL_COMMIT"; then
    [[ -f "$ROOT/lib/pico-sdk/CMakeLists.txt" ]] || {
      echo "error: lib/pico-sdk is missing; run: git submodule update --init lib/pico-sdk" >&2
      exit 1
    }
    local tmp
    tmp="$(mktemp -d)"
    git -c advice.detachedHead=false clone -q --depth 1 --branch "$PICOTOOL_VERSION" \
      https://github.com/raspberrypi/picotool.git "$tmp/src"
    [[ "$(git -C "$tmp/src" rev-parse HEAD)" == "$PICOTOOL_COMMIT" ]] || {
      echo "error: picotool tag $PICOTOOL_VERSION is not commit $PICOTOOL_COMMIT" >&2
      exit 1
    }
    rm -rf "$dir"
    "$cmake" -S "$tmp/src" -B "$tmp/build" -DCMAKE_BUILD_TYPE=Release \
      -DPICO_SDK_PATH="$ROOT/lib/pico-sdk" -DPICOTOOL_NO_LIBUSB=1 \
      -DPICOTOOL_FLAT_INSTALL=1 -DCMAKE_INSTALL_PREFIX="$dir" >/dev/null
    "$cmake" --build "$tmp/build" --parallel "$(nproc)" >/dev/null
    "$cmake" --install "$tmp/build" >/dev/null
    rm -rf "$tmp"
    echo "$PICOTOOL_COMMIT" > "$dir/.version"
  fi
  "$dir/picotool/picotool" version
}

for c in "${components[@]}"; do
  case "$c" in
    cmake) install_cmake ;;
    arm) install_arm ;;
    picotool) install_picotool ;;
    *) echo "error: unknown component '$c'" >&2; exit 1 ;;
  esac
done
