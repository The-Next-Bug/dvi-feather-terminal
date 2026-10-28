#!/usr/bin/env bash
# Build the user guide (docs/guide, bd 1ay) as a static HTML site:
# build/docs/html/index.html. scripts/package.sh puts it in the release ZIP.
#
# Steps: build docgen with the host tests' build (test/CMakeLists.txt) and
# run it (screenshots from the firmware's own portable code, the settings
# tables), turn its PPMs into PNGs, write the tables from headers
# (gen_tables.py: HWINFO items, board pins) and the version, stage them
# with docs/guide/src, run mdBook, then check every internal link.
#
# Environment overrides:
#   DOCS_BUILD_DIR  output root (default: build/docs)
#   TEST_BUILD_DIR  the host build docgen is built in (default: build/test)
#   TOOLCHAIN_DIR   toolchain location (default: <repo>/.toolchain)
#   CMAKE           cmake binary (default: $TOOLCHAIN_DIR/cmake/bin/cmake, else cmake on PATH)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DOCS_BUILD_DIR="${DOCS_BUILD_DIR:-build/docs}"
TEST_BUILD_DIR="${TEST_BUILD_DIR:-build/test}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$ROOT/.toolchain}"
if [[ -z "${CMAKE:-}" ]]; then
  CMAKE="$TOOLCHAIN_DIR/cmake/bin/cmake"
  [[ -x "$CMAKE" ]] || CMAKE=cmake
fi
MDBOOK="$TOOLCHAIN_DIR/mdbook/mdbook"
[[ -x "$MDBOOK" ]] || { echo "error: $MDBOOK missing; run scripts/setup-toolchain.sh mdbook" >&2; exit 1; }

# docgen, configured as scripts/test.sh configures the tests.
"$CMAKE" -S test -B "$TEST_BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug \
  -DCPU_TESTS="$([[ "${CPU_TESTS:-}" == 1 ]] && echo ON || echo OFF)" >/dev/null
"$CMAKE" --build "$TEST_BUILD_DIR" --parallel "$(nproc)" --target docgen >/dev/null

book="$DOCS_BUILD_DIR/book"
gen="$DOCS_BUILD_DIR/gen"
rm -rf "$book" "$gen" "$DOCS_BUILD_DIR/html"
mkdir -p "$book" "$gen"
cp docs/guide/book.toml "$book/"
cp -r docs/guide/src "$book/src"

# Generated parts, under src/generated (Markdown, included by the pages)
# and src/images/generated (PNG).
generated="$book/src/generated"
mkdir -p "$generated" "$book/src/images/generated"
"$TEST_BUILD_DIR/docgen" "$gen"
python3 -I docs/guide/gen/ppm2png.py "$gen"/*.ppm "$book/src/images/generated"
cp "$gen"/*.md "$generated/"
python3 -I docs/guide/gen/gen_tables.py "$ROOT" "$generated" "${CC:-cc}"
version="$(git describe --always --dirty 2>/dev/null || echo unknown)"
printf '%s\n' "$version" > "$generated/version.md"

"$MDBOOK" build "$book" --dest-dir "$ROOT/$DOCS_BUILD_DIR/html" >/dev/null
python3 -I docs/guide/gen/checklinks.py "$DOCS_BUILD_DIR/html"
echo "guide $version: $DOCS_BUILD_DIR/html/index.html"
