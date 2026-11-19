#!/usr/bin/env bash
# Package finished builds into a release ZIP: for every target built under
# BUILD_DIR (but experimental boards: boards/experimental/README.md), a
# folder <target>/ with its UF2 as FIRMWARE.UF2 (once: the name SET-UP's
# firmware update looks for, and BOOTSEL takes any name), its BIN and
# build-info.txt; plus flashing instructions, the third-party notices and license texts, CHANGES.md (from the git history since the previous tag), SHA256SUMS and the
# source of the built commit; and the user guide (scripts/docs.sh) as
# guide/, also on its own as <release>-guide.zip. CHANGES.md is also written
# next to the ZIP for use as release notes.
#
# The WiFi modem's firmware (scripts/build-zimodem.sh, BUILD_DIR/zimodem)
# goes into modem/ with its licenses, source and docs/MODEM.txt; without
# it the ZIP has no modem/ (an error with REQUIRE_ZIMODEM=1, as in CI).
#
# Tagged builds (tags like v0.1.0) are named dvi-terminal-<tag>; anything
# else dvi-terminal-<git describe>.
#
# Environment overrides:
#   BUILD_DIR   build output root from build.sh (default: build)
#   REQUIRE_ZIMODEM  1: fail without the modem's firmware
#   DIST_DIR    where the ZIP is written        (default: dist)
#   DOCS_DIR    the built user guide            (default: build/docs/html)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
DIST_DIR="${DIST_DIR:-dist}"
DOCS_DIR="${DOCS_DIR:-build/docs/html}"
[[ -f "$DOCS_DIR/index.html" ]] || { echo "error: no user guide in $DOCS_DIR; run scripts/docs.sh" >&2; exit 1; }

info() { sed -n "s/^$2=//p" "$1"; }
infos=()
for f in "$BUILD_DIR"/*/build-info.txt; do
  [[ -f "$f" ]] || continue
  [[ "$f" != "$BUILD_DIR/zimodem/build-info.txt" ]] || continue # the modem's (below)
  if [[ "$(info "$f" experimental)" == yes ]]; then
    echo "skipping experimental build $(dirname "$f")" >&2
    continue
  fi
  infos+=("$f")
done
((${#infos[@]})) || { echo "error: no builds in $BUILD_DIR; run scripts/build.sh" >&2; exit 1; }

rev="$(info "${infos[0]}" git_describe)"
sha="$(info "${infos[0]}" git_sha)"
for f in "${infos[@]}"; do
  [[ "$(info "$f" git_sha)" == "$sha" ]] || { echo "error: $f is from another commit" >&2; exit 1; }
done

release="dvi-terminal-$rev"
stage="$DIST_DIR/$release"
rm -rf "$stage"
mkdir -p "$stage"

for f in "${infos[@]}"; do
  dir="$(dirname "$f")"
  target="$(info "$f" target)"
  mkdir -p "$stage/$target"
  cp "$dir/dvi_terminal.uf2" "$stage/$target/FIRMWARE.UF2"
  cp "$dir/dvi_terminal.bin" "$stage/$target/dvi_terminal.bin"
  cp "$f" "$stage/$target/build-info.txt"
done
cp LICENSE docs/FLASHING.txt docs/THIRD-PARTY.md "$stage/"

# License texts of the third-party code and data in the firmware
# (docs/THIRD-PARTY.md lists them), from the sources the build used.
mkdir -p "$stage/licenses"
license() {
  [[ -f "$1" ]] || { echo "error: license text $1 missing (submodules checked out?)" >&2; exit 1; }
  cp "$1" "$stage/licenses/$2"
}
license lib/pico-sdk/LICENSE.TXT pico-sdk-BSD-3-Clause.txt
license lib/pico-sdk/src/rp2_common/cmsis/stub/CMSIS/LICENSE.txt Apache-2.0.txt
license lib/tinyusb/LICENSE tinyusb-MIT.txt
license lib/z80pack/LICENSE z80pack-MIT.txt
license lib/fatfs/LICENSE.txt FatFs-BSD-1-Clause.txt
license assets/fonts/Terminus-OFL.txt Terminus-OFL-1.1.txt
license docs/licenses/GPL-2.0.txt GPL-2.0.txt

# The WiFi modem's firmware (bd xir.17.9), built from lib/zimodem.
zim="$BUILD_DIR/zimodem"
modem_bin=
if [[ -f "$zim/zimodem.bin" ]]; then
  mkdir -p "$stage/modem"
  cp -r "$zim/zimodem.bin" "$zim/build-info.txt" "$zim/zimodem-source.zip" "$zim/licenses" "$stage/modem/"
  cp docs/MODEM.txt "$stage/modem/README.txt"
  modem_bin=modem/zimodem.bin
elif [[ "${REQUIRE_ZIMODEM:-0}" == 1 ]]; then
  echo "error: no WiFi modem firmware in $zim; run scripts/build-zimodem.sh" >&2
  exit 1
else
  echo "warning: no WiFi modem firmware in $zim (scripts/build-zimodem.sh): no modem/ in the ZIP" >&2
fi

scripts/changelog.sh "$sha" | tee "$stage/CHANGES.md" > "$DIST_DIR/CHANGES.md"
(cd "$stage" && sha256sum -- */FIRMWARE.UF2 */dvi_terminal.bin ${modem_bin:+"$modem_bin"} > SHA256SUMS)

# Source of exactly the commit that was built. The submodules (pico-sdk,
# TinyUSB) are not included; their commits are recorded in <target>/build-info.txt
# and the source's git tree.
mkdir -p "$stage/source"
git archive --format=tar "$sha" | tar -x -C "$stage/source"

# The user guide (bd 1ay): guide/index.html, readable offline.
cp -r "$DOCS_DIR" "$stage/guide"
guide="$release-guide"
rm -rf "$DIST_DIR/$guide"
cp -r "$DOCS_DIR" "$DIST_DIR/$guide"

(cd "$DIST_DIR" && rm -f "$release.zip" "$guide.zip" && python3 -m zipfile -c "$release.zip" "$release" \
  && python3 -m zipfile -c "$guide.zip" "$guide" && rm -rf "$guide")
echo "$DIST_DIR/$release.zip"
echo "$DIST_DIR/$guide.zip"
