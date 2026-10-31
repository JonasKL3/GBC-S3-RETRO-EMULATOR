#!/usr/bin/env sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BIN="${TMPDIR:-/tmp}/gbc_s3_path_tests.$$"
trap 'rm -f "$BIN"' EXIT
c++ -std=c++11 -Wall -Wextra -Werror "$ROOT/tests/test_gbc_paths.cpp" -o "$BIN"
"$BIN"
