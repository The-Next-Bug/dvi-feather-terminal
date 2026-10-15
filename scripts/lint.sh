#!/usr/bin/env bash
# Lint our C sources with clang-tidy (the checks are in .clang-tidy) as
# each build compiles them, from its compile_commands.json: the host tests'
# (portable libraries and tests) and a firmware target's (RP2350 code too).
# Code under an #if is checked only as those builds configure it.
#
# Usage: scripts/lint.sh [build dir...]
#   default: build/test and build/$TARGET (TARGET default pico2-dvi-sock);
#   build them first (scripts/test.sh, scripts/build.sh): some headers are
#   generated. CI lints build/test in the test job and each target in its
#   build job.
#
# Environment overrides:
#   TOOLCHAIN_DIR   toolchain location (default: <repo>/.toolchain)
#   CLANG_TIDY      clang-tidy binary (default: $TOOLCHAIN_DIR/clang-tidy/bin/clang-tidy,
#                   from scripts/setup-toolchain.sh clang-tidy)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$ROOT/.toolchain}"
CLANG_TIDY="${CLANG_TIDY:-$TOOLCHAIN_DIR/clang-tidy/bin/clang-tidy}"
[[ -x "$CLANG_TIDY" ]] || {
  echo "error: $CLANG_TIDY missing; run scripts/setup-toolchain.sh clang-tidy" >&2
  exit 1
}

dirs=("$@")
[[ ${#dirs[@]} -gt 0 ]] || dirs=(build/test "build/${TARGET:-pico2-dvi-sock}")

status=0
for dir in "${dirs[@]}"; do
  db="$dir/compile_commands.json"
  [[ -f "$db" ]] || {
    echo "error: $db missing; build it first (scripts/test.sh, scripts/build.sh)" >&2
    exit 1
  }
  # Our sources only: not lib/ (vendored) nor generated ones in the build.
  mapfile -t files < <(python3 - "$db" "$ROOT" <<'PY'
import json, os, sys
root = sys.argv[2]
seen = set()
for e in json.load(open(sys.argv[1])):
    f = os.path.normpath(os.path.join(e['directory'], e['file']))
    rel = os.path.relpath(f, root)
    if rel.split(os.sep)[0] in ('apps', 'boards', 'src', 'test') and f not in seen:
        seen.add(f)
        print(f)
PY
  )
  extra=()
  # clang reads the Arm GCC's command lines given the target and newlib.
  gcc="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))[0]["command"].split()[0])' "$db")"
  if [[ "$gcc" == *arm-none-eabi-gcc ]]; then
    sysroot="$("$gcc" -print-sysroot)"
    extra=(--extra-arg=--target=arm-none-eabi "--extra-arg=--sysroot=$sysroot"
      --extra-arg=-Wno-unknown-warning-option --extra-arg=-Wno-unused-command-line-argument)
  fi
  echo "lint: ${#files[@]} files in $dir"
  # Diagnostics go to stdout; stderr has only progress and counts (of
  # warnings suppressed in lib/ headers) and "Error while processing",
  # which the exit status carries.
  printf '%s\0' "${files[@]}" |
    xargs -0 -n 8 -P "$(nproc)" "$CLANG_TIDY" -p "$dir" --quiet "${extra[@]}" 2>/dev/null ||
    status=1
done
exit $status
