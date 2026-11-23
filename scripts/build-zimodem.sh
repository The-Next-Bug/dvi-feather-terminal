#!/usr/bin/env bash
# Build the WiFi modem's firmware (bd xir.17.9): our Zimodem fork in
# lib/zimodem (branch frankless-build), with its own scripts/build.sh and the
# toolchain scripts/setup-toolchain.sh installs as its "zimodem" component
# (arduino-cli and the ESP8266 Arduino core, into TOOLCHAIN_DIR/arduino15).
#
# Output in BUILD_DIR (default build/zimodem): zimodem.bin (the image an
# ESP-12F flashes at 0x0), ZIMODEM.ZMI (the same in the container the
# terminal flashes from, src/modem/zmi.h, bd xir.17.13: module esp12f,
# Zimodem's version, the flash size from the FQBN's eesz), build-info.txt
# (Zimodem's, plus our lines),
# licenses/ (the texts of everything in the image, docs/THIRD-PARTY.md's
# "WiFi modem firmware" table, copied from the sources and the toolchain
# the build used) and sources/: zimodem-<commit>.zip (the submodule's
# commit, the image's own source) and the ESP8266 core's release archive
# (its source, LGPL-2.1, linked into the image; arduino-cli's download,
# else fetched again, checked against the package index's SHA-256).
# scripts/package.sh puts them in the release ZIP's modem/ folder.
#
# The version Zimodem reports (ATI4) is 4.0.3-<BUILD_ID>; BUILD_ID defaults
# to the submodule commit's time (UTC, YYYYMMDDHHMMSS), so the same commit
# always builds the same version.
#
# MODULE=xhc3f (bd xir.17.12.4) builds for the Sparkleiot XH-C3F (ESP32-C3,
# the frankless-wifi carrier; needs scripts/setup-toolchain.sh
# zimodem-esp32) into build/zimodem-xhc3f: zimodem-xhc3f.bin (bootloader,
# partition table, boot_app0 and app merged, to flash at 0x0 with esptool
# over the C3's USB or a USB-serial adapter), build-info.txt, licenses/ and
# sources/ (docs/THIRD-PARTY.md's "WiFi modem firmware: XH-C3F"): Zimodem's
# source, and the ESP32 Arduino core's LGPL-2.1 part (its release archive
# without the four chips' precompiled ESP-IDF libraries, tools/sdk, which
# are Apache-2.0; the archive's URL and SHA-256 are in build-info.txt).
# License texts not in the toolchain (ESP-IDF's LICENSE and COPYRIGHT.rst,
# wpa_supplicant's, newlib's, SPIFFS's, the core's own LGPL text) are
# fetched from their tags, checked against the SHA-256s below. No
# ZIMODEM.ZMI yet: the terminal cannot flash a C3 (bd xir.17.12.5/.6).
#
# Environment overrides:
#   MODULE             esp12f (default) or xhc3f
#   BUILD_DIR          output directory (default: build/zimodem, or
#                      build/zimodem-xhc3f)
#   BUILD_ID           digits for the version (default: the commit's time)
#   FRANKLESS_HW_FLOW  passed on (see lib/zimodem/BUILDING.md)
#   TOOLCHAIN_DIR      as scripts/setup-toolchain.sh (default: <repo>/.toolchain)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ZROOT="$ROOT/lib/zimodem"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$ROOT/.toolchain}"
MODULE="${MODULE:-esp12f}"
case "$MODULE" in
  esp12f) BUILD_DIR="$(realpath -m "${BUILD_DIR:-build/zimodem}")" ;;
  xhc3f) BUILD_DIR="$(realpath -m "${BUILD_DIR:-build/zimodem-xhc3f}")" ;;
  *) echo "error: MODULE must be esp12f or xhc3f (got '$MODULE')" >&2; exit 1 ;;
esac
[[ -f "$ZROOT/scripts/build.sh" ]] || { echo "error: lib/zimodem missing (git submodule update --init)" >&2; exit 1; }
cli="$TOOLCHAIN_DIR/arduino15/bin/arduino-cli"
[[ -x "$cli" ]] || { echo "error: no arduino-cli in $TOOLCHAIN_DIR; run scripts/setup-toolchain.sh zimodem" >&2; exit 1; }
if [[ "$MODULE" == xhc3f && ! -f "$TOOLCHAIN_DIR/arduino15/.version-esp32" ]]; then
  echo "error: no ESP32 core in $TOOLCHAIN_DIR; run scripts/setup-toolchain.sh zimodem-esp32" >&2
  exit 1
fi

BUILD_ID="${BUILD_ID:-$(TZ=UTC git -C "$ZROOT" log -1 --format=%cd --date=format-local:%Y%m%d%H%M%S)}"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
ARDUINO_DIRECTORIES_DATA="$TOOLCHAIN_DIR/arduino15" ARDUINO_DIRECTORIES_USER="$TOOLCHAIN_DIR/arduino-user" \
  ARDUINO_CLI="$cli" BUILD_DIR="$BUILD_DIR" BUILD_ID="$BUILD_ID" MODULE="$MODULE" "$ZROOT/scripts/build.sh"

if [[ "$MODULE" == xhc3f ]]; then
  cp "$BUILD_DIR/zimodem.ino.merged.bin" "$BUILD_DIR/zimodem-xhc3f.bin"
  rm -rf "$BUILD_DIR/work"
  core_version="$(sed -n 's/^esp32_core=//p' "$BUILD_DIR/build-info.txt")"
  core="$TOOLCHAIN_DIR/arduino15/packages/esp32/hardware/esp32/$core_version"
  sdk="$core/tools/sdk/esp32c3/include"
  lic="$BUILD_DIR/licenses"
  mkdir -p "$lic" "$BUILD_DIR/sources"
  license() {
    [[ -f "$1" ]] || { echo "error: license text $1 missing" >&2; exit 1; }
    cp "$1" "$lic/$2"
  }
  from_header() { # header, file name, markers...
    local header="$1" name="$2"
    shift 2
    python3 "$ROOT/scripts/header_license.py" "$header" "$@" > "$lic/$name"
  }
  # name, URL, SHA-256: texts the toolchain lacks, at the versions built
  # (ESP-IDF v4.4.7 is core 2.0.17's; SPIFFS at the commit that pins).
  fetched() {
    curl -sSfL -o "$lic/$1" "$2"
    echo "$3  $lic/$1" | sha256sum -c - >&2
  }
  license "$ZROOT/LICENSE" Zimodem-Apache-2.0.txt
  license "$ZROOT/NOTICE" Zimodem-NOTICE.txt
  from_header "$ZROOT/zimodem/src/libssh2/libssh2.h" libssh2-BSD-3-Clause.txt "Sara Golemon" "Redistribution and use"
  fetched arduino-esp32-LGPL-2.1.txt https://raw.githubusercontent.com/espressif/arduino-esp32/2.0.17/LICENSE.md \
    994ce1902a23c74519b5d118c6bbb657edabce91dd6b7cc825d86bb8f5c9780f
  fetched ESP-IDF-Apache-2.0.txt https://raw.githubusercontent.com/espressif/esp-idf/v4.4.7/LICENSE \
    cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30
  fetched ESP-IDF-COPYRIGHT.rst https://raw.githubusercontent.com/espressif/esp-idf/v4.4.7/docs/en/COPYRIGHT.rst \
    a1db55f6af33968650e8c53553fc58386bf3d871d740ccfe464c73c60779acc7
  fetched wpa_supplicant-BSD.txt https://raw.githubusercontent.com/espressif/esp-idf/v4.4.7/components/wpa_supplicant/COPYING \
    e9f84e02bb9ba965464e21604014f13d0f3c9299a1b5f7882574a9b4a261abe7
  fetched newlib-COPYING.NEWLIB.txt https://raw.githubusercontent.com/espressif/esp-idf/v4.4.7/components/newlib/COPYING.NEWLIB \
    0681089a556e93791da82718d68011ba452de245f7f59c3846936304756ac0c0
  fetched SPIFFS-MIT.txt https://raw.githubusercontent.com/pellepl/spiffs/0dbb3f71c5f6fae3747a9d935372773762baf852/LICENSE \
    d19257540156a51ed50c2378e7159066415d8bbbe147779f8deb0d3e73f1b0a7
  from_header "$sdk/freertos/include/freertos/FreeRTOS.h" FreeRTOS-MIT.txt "Amazon.com" "Permission is hereby granted"
  from_header "$sdk/lwip/lwip/src/include/lwip/opt.h" lwIP-BSD-3-Clause.txt "Swedish Institute" "OF SUCH DAMAGE"
  from_header "$sdk/fatfs/src/ff.h" FatFs-ChaN.txt "ChaN" "Redistribution and use"
  from_header "$sdk/json/cJSON/cJSON.h" cJSON-MIT.txt "Dave Gamble" "Permission is hereby granted"
  from_header "$sdk/nghttp/port/include/nghttp2/nghttp2ver.h" nghttp2-MIT.txt "Tatsuhiro Tsujikawa" "Permission is hereby granted"
  from_header "$sdk/protobuf-c/protobuf-c/protobuf-c/protobuf-c.h" protobuf-c-BSD-2-Clause.txt "Dave Benson" "Redistribution and use"

  # The sources: Zimodem at the commit built, and the core's LGPL part from
  # the release archive arduino-cli installed (else fetched), checked
  # against the package index.
  zsha="$(git -C "$ZROOT" rev-parse --short HEAD)"
  git -C "$ZROOT" archive --format=zip --prefix="zimodem-$zsha/" -o "$BUILD_DIR/sources/zimodem-$zsha.zip" HEAD
  read -r core_url core_sha < <(python3 - "$TOOLCHAIN_DIR/arduino15/package_esp32_index.json" "$core_version" <<'PY'
import json, sys
index, version = sys.argv[1], sys.argv[2]
for package in json.load(open(index))["packages"]:
    for platform in package["platforms"]:
        if platform["architecture"] == "esp32" and platform["version"] == version:
            print(platform["url"], platform["checksum"].removeprefix("SHA-256:"))
            sys.exit(0)
sys.exit("ESP32 core " + version + " not in " + index)
PY
)
  archive="$TOOLCHAIN_DIR/arduino15/staging/packages/${core_url##*/}"
  if [[ ! -f "$archive" ]]; then
    archive="$BUILD_DIR/work-${core_url##*/}"
    curl -sSfL -o "$archive" "$core_url"
  fi
  echo "$core_sha  $archive" | sha256sum -c - >&2
  core_src="$BUILD_DIR/sources/arduino-esp32-$core_version-lgpl.zip"
  python3 - "$archive" "$core_src" "$lic/arduino-esp32-LGPL-2.1.txt" <<'PY'
import sys, zipfile
src, dst, license_text = sys.argv[1:]
with zipfile.ZipFile(src) as zin, zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as zout:
    top = zin.namelist()[0].split("/")[0]
    for info in zin.infolist():
        rest = info.filename.split("/", 1)[1] if "/" in info.filename else ""
        # the ESP-IDF libraries (Apache-2.0, binaries) and Windows tools
        if rest.startswith("tools/sdk/") or rest.endswith(".exe"):
            continue
        zout.writestr(info, zin.read(info))
    zout.write(license_text, top + "/LICENSE.md")
PY
  rm -f "$BUILD_DIR"/work-*.zip
  {
    echo "image=zimodem-xhc3f.bin (merged: flash at 0x0)"
    echo "image_size=$(stat -c %s "$BUILD_DIR/zimodem-xhc3f.bin")"
    echo "image_sha256=$(sha256sum "$BUILD_DIR/zimodem-xhc3f.bin" | cut -d' ' -f1)"
    echo "zimodem_source=sources/zimodem-$zsha.zip"
    echo "core_source=sources/${core_src##*/} (the LGPL part of $core_url)"
    echo "core_archive_sha256=$core_sha"
    echo "terminal_git_describe=$(git describe --always --dirty 2>/dev/null || echo unknown)"
  } >> "$BUILD_DIR/build-info.txt"
  cat "$BUILD_DIR/build-info.txt"
  exit 0
fi

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

# The sources: the submodule at the commit built (Apache-2.0), and the
# ESP8266 core's release, which arduino-cli installed from (LGPL-2.1).
mkdir -p "$BUILD_DIR/sources"
zsha="$(git -C "$ZROOT" rev-parse --short HEAD)"
git -C "$ZROOT" archive --format=zip --prefix="zimodem-$zsha/" -o "$BUILD_DIR/sources/zimodem-$zsha.zip" HEAD
core_version="${core##*/}"
read -r core_url core_sha < <(python3 - "$TOOLCHAIN_DIR/arduino15/package_esp8266com_index.json" "$core_version" <<'PY'
import json, sys
index, version = sys.argv[1], sys.argv[2]
for package in json.load(open(index))["packages"]:
    for platform in package["platforms"]:
        if platform["architecture"] == "esp8266" and platform["version"] == version:
            print(platform["url"], platform["checksum"].removeprefix("SHA-256:"))
            sys.exit(0)
sys.exit("ESP8266 core " + version + " not in " + index)
PY
)
core_zip="$BUILD_DIR/sources/${core_url##*/}"
staged="$TOOLCHAIN_DIR/arduino15/staging/packages/${core_url##*/}"
if [[ -f "$staged" ]]; then
  cp "$staged" "$core_zip"
else
  curl -sSfL -o "$core_zip" "$core_url"
fi
echo "$core_sha  $core_zip" | sha256sum -c - >&2
# The container: the version ATI4 reports, the flash size the image is
# built for (the FQBN's eesz=4M1M: 4M).
version="$(sed -n 's/^firmware_version=//p' "$BUILD_DIR/build-info.txt")"
flash="$(sed -n 's/^fqbn=.*eesz=\([0-9]*[KM]\).*/\1/p' "$BUILD_DIR/build-info.txt")"
[[ -n "$version" && -n "$flash" ]] || { echo "error: no version or flash size in build-info.txt" >&2; exit 1; }
python3 "$ROOT/src/modem/zmi_gen.py" --module esp12f --version "$version" --flash-size "$flash" \
  "$BUILD_DIR/ZIMODEM.ZMI" "0x0:$BUILD_DIR/zimodem.bin"
{
  echo "container=ZIMODEM.ZMI"
  echo "container_sha256=$(sha256sum "$BUILD_DIR/ZIMODEM.ZMI" | cut -d' ' -f1)"
  echo "image=zimodem.bin"
  echo "image_size=$(stat -c %s "$BUILD_DIR/zimodem.bin")"
  echo "image_sha256=$(sha256sum "$BUILD_DIR/zimodem.bin" | cut -d' ' -f1)"
  echo "module=esp12f"
  echo "zimodem_source=sources/zimodem-$zsha.zip"
  echo "core_source=sources/${core_url##*/} ($core_url)"
  echo "terminal_git_describe=$(git describe --always --dirty 2>/dev/null || echo unknown)"
} >> "$BUILD_DIR/build-info.txt"
cat "$BUILD_DIR/build-info.txt"
