#!/usr/bin/env bash
# Write Markdown release notes for REF (default HEAD) from the commit history
# since the previous tag. With no previous tag, the whole history is used.
#
# Usage: changelog.sh [REF] > CHANGES.md
set -euo pipefail

ref="${1:-HEAD}"
title="$(git describe --tags --exact-match "$ref" 2>/dev/null || git rev-parse --short "$ref")"

if prev="$(git describe --tags --abbrev=0 "$ref^" 2>/dev/null)"; then
  range="$prev..$ref"
  since="Changes since $prev"
else
  range="$ref"
  since="Changes"
fi

echo "# $title"
echo
echo "## $since"
echo
git log --no-merges --reverse --format='- %s (%h)' "$range"
