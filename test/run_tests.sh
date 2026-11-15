#!/usr/bin/env bash
# Builds and runs the unit tests (test/*_test.c, no game data, no display needed): exit 0 only
# if every check of every test passed.
set -u
cd "$(dirname "$0")/.."
CC=${CC:-gcc}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FLAGS=(-std=gnu11 -O1 -Wall -Wno-unused-function -D_GNU_SOURCE=1 -Isrc -Itest)
fail=0
build() { # name, sources...
  local name=$1
  shift
  if ! "$CC" "${FLAGS[@]}" "$@" -o "$TMP/$name" -lm; then
    echo "FAILED TO COMPILE $name"
    fail=1
    return 1
  fi
  if ! "$TMP/$name"; then
    echo "FAILED $name"
    fail=1
  fi
}
build vroom_test test/vroom_test.c src/vroom.c
build net_version_test test/net_version_test.c src/net.c src/vroom.c
if [ "$fail" -ne 0 ]; then
  echo "SOME TESTS FAILED"
  exit 1
fi
echo "all unit tests passed"
