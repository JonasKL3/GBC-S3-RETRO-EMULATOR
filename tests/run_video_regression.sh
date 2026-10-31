#!/usr/bin/env sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD="$(mktemp -d)"
trap 'rm -rf "$BUILD"' EXIT
g++ -std=gnu++11 "$ROOT/tests/test_video_regression.cpp" -o "$BUILD/test_video"
"$BUILD/test_video"
printf '%s\n' 'Video cadence and full-frame/stripe equivalence: PASS'
