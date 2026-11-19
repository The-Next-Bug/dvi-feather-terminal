#!/usr/bin/env bash
# Build the WiFi modem's firmware (bd xir.17.9): our Zimodem fork in
# lib/zimodem (branch frankless-build), with its own scripts/build.sh and the
# toolchain scripts/setup-toolchain.sh installs as its "zimodem" component
# (arduino-cli and the ESP8266 Arduino core, into TOOLCHAIN_DIR/arduino15).
#
# Output in BUILD_DIR (default build/zimodem): zimodem.bin (the image an
# ESP-12F flashes at 0x0), build-info.txt (Zimodem's, plus our lines),
# licenses/ (the texts of everything in the image, docs/THIRD-PARTY.md's
# "WiFi modem firmware" table, copied from the sources and the toolchain
# the build used) and zimodem-source.zip (the submodule's commit, the
# image's own source). scripts/package.sh puts them in the release ZIP's
# modem/ folder.
#
# The version Zimodem reports (ATI4) is 4.0.3-<BUILD_ID>; BUILD_ID defaults
# to the submodule commit's time (UTC, YYYYMMDDHHMMSS), so the same commit
# always builds the same version.
#
# Environment overrides:
#   BUILD_DIR          output directory (default: build/zimodem)
#   BUILD_ID           digits for the version (default: the commit's time)
#   FRANKLESS_HW_FLOW  passed on (see lib/zimodem/BUILDING.md)
#   TOOLCHAIN_DIR      as scripts/setup-toolchain.sh (default: <repo>/.toolchain)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ZROOT="$ROOT/lib/zimodem"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$ROOT/.toolchain}"
BUILD_DIR="$(realpath -m "${BUILD_DIR:-build/zimodem}")"
[[ -f "$ZROOT/scripts/build.sh" ]] || { echo "error: lib/zimodem missing (git submodule update --init)" >&2; exit 1; }
cli="$TOOLCHAIN_DIR/arduino15/bin/arduino-cli"
[[ -x "$cli" ]] || { echo "error: no arduino-cli in $TOOLCHAIN_DIR; run scripts/setup-toolchain.sh zimodem" >&2; exit 1; }

BUILD_ID="${BUILD_ID:-$(TZ=UTC git -C "$ZROOT" log -1 --format=%cd --date=format-local:%Y%m%d%H%M%S)}"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
ARDUINO_DIRECTORIES_DATA="$TOOLCHAIN_DIR/arduino15" ARDUINO_DIRECTORIES_USER="$TOOLCHAIN_DIR/arduino-user" \
  ARDUINO_CLI="$cli" BUILD_DIR="$BUILD_DIR" BUILD_ID="$BUILD_ID" "$ZROOT/scripts/build.sh"

# The image under a plain name, the work directory dropped (hundreds of MB
# of objects; BUILD_DIR/work stays only on failure).
mv "$BUILD_DIR/zimodem.ino.bin" "$BUILD_DIR/zimodem.bin"
rm -rf "$BUILD_DIR/work"

# The license texts (docs/THIRD-PARTY.md lists what each covers).
core="$TOOLCHAIN_DIR/arduino15/packages/esp8266/hardware/esp8266/$(sed -n 's/^esp8266_core=//p' "$BUILD_DIR/build-info.txt")"
lic="$BUILD_DIR/licenses"
mkdir -p "$lic"
license() {
  [[ -f "$1" ]] || { echo "error: license text $1 missing" >&2; exit 1; }
  cp "$1" "$lic/$2"
}
license "$ZROOT/LICENSE" Zimodem-Apache-2.0.txt
license "$ZROOT/NOTICE" Zimodem-NOTICE.txt
license "$core/LICENSE" esp8266-arduino-LGPL-2.1.txt
license "$core/tools/sdk/License" Espressif-MIT.txt
license "$core/cores/esp8266/umm_malloc/LICENSE" umm_malloc-MIT.txt
license "$core/cores/esp8266/spiffs/LICENSE" SPIFFS-MIT.txt
license "$core/bootloaders/eboot/LICENSE" eboot-BSD-3-Clause.txt
# lwIP has no license file in the core: its header comment is the license.
opt="$core/tools/sdk/lwip2/include/lwip/opt.h"
[[ -f "$opt" ]] || { echo "error: $opt missing" >&2; exit 1; }
sed -n '/Copyright (c) 2001-2004 Swedish Institute/,/OF SUCH DAMAGE/p' "$opt" | sed 's/^ \* \{0,1\}//' > "$lic/lwIP-BSD-3-Clause.txt"
[[ -s "$lic/lwIP-BSD-3-Clause.txt" ]] || { echo "error: lwIP's license not found in $opt" >&2; exit 1; }

# The image's source: the submodule at the commit built (Apache-2.0).
git -C "$ZROOT" archive --format=zip --prefix=zimodem/ -o "$BUILD_DIR/zimodem-source.zip" HEAD
{
  echo "image=zimodem.bin"
  echo "image_size=$(stat -c %s "$BUILD_DIR/zimodem.bin")"
  echo "image_sha256=$(sha256sum "$BUILD_DIR/zimodem.bin" | cut -d' ' -f1)"
  echo "module=esp12f"
  echo "terminal_git_describe=$(git describe --always --dirty 2>/dev/null || echo unknown)"
} >> "$BUILD_DIR/build-info.txt"
cat "$BUILD_DIR/build-info.txt"
