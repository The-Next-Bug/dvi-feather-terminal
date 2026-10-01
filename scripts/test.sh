#!/usr/bin/env bash
# Build and run the host unit tests (portable terminal code, no pico-sdk).
#
# Environment overrides:
#   TEST_BUILD_DIR  build directory (default: build/test)
#   CMAKE           cmake binary    (default: .toolchain/cmake/bin/cmake, else cmake on PATH)
#   CPU_TESTS=1     also run the Z80 instruction exercisers (test_cpu: minutes)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TEST_BUILD_DIR="${TEST_BUILD_DIR:-build/test}"
if [[ -z "${CMAKE:-}" ]]; then
  CMAKE="$ROOT/.toolchain/cmake/bin/cmake"
  [[ -x "$CMAKE" ]] || CMAKE=cmake
fi
CTEST="$(dirname "$(command -v "$CMAKE")")/ctest"

"$CMAKE" -S test -B "$TEST_BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug \
  -DCPU_TESTS="$([[ "${CPU_TESTS:-}" == 1 ]] && echo ON || echo OFF)" >/dev/null
"$CMAKE" --build "$TEST_BUILD_DIR" --parallel "$(nproc)"
"$CTEST" --test-dir "$TEST_BUILD_DIR" --output-on-failure --output-junit junit.xml
