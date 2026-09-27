#!/usr/bin/env bash
# Package finished builds into a release ZIP: for every target built under
# BUILD_DIR, its UF2, BIN and build-info; plus flashing instructions,
# CHANGES.md (from the git history since the previous tag), SHA256SUMS and the
# source of the built commit. CHANGES.md is also written next to the ZIP for
# use as release notes.
#
# Tagged builds (tags like v0.1.0) are named dvi-terminal-<tag>; anything
# else dvi-terminal-<git describe>.
#
# Environment overrides:
#   BUILD_DIR   build output root from build.sh (default: build)
#   DIST_DIR    where the ZIP is written        (default: dist)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
DIST_DIR="${DIST_DIR:-dist}"

infos=("$BUILD_DIR"/*/build-info.txt)
[[ -f "${infos[0]}" ]] || { echo "error: no builds in $BUILD_DIR; run scripts/build.sh" >&2; exit 1; }

info() { sed -n "s/^$2=//p" "$1"; }
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
  cp "$dir/dvi_terminal.uf2" "$stage/dvi_terminal-$target.uf2"
  cp "$dir/dvi_terminal.bin" "$stage/dvi_terminal-$target.bin"
  cp "$f" "$stage/build-info-$target.txt"
done
cp docs/FLASHING.txt "$stage/"
scripts/changelog.sh "$sha" | tee "$stage/CHANGES.md" > "$DIST_DIR/CHANGES.md"
(cd "$stage" && sha256sum -- *.uf2 *.bin > SHA256SUMS)

# Source of exactly the commit that was built. Submodules (pico-sdk, PicoDVI)
# are not included; their commits are recorded in build-info-*.txt.
mkdir -p "$stage/source"
git archive --format=tar "$sha" | tar -x -C "$stage/source"

(cd "$DIST_DIR" && rm -f "$release.zip" && python3 -m zipfile -c "$release.zip" "$release")
echo "$DIST_DIR/$release.zip"
