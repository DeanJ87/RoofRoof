#!/usr/bin/env bash
# Differential fuzz of the close sequence (spec 014 SC-002): builds
# tools/close-diff/driver.cpp against this tree's lib/ and against a base
# revision's (default origin/main), runs both on the same seeds and compares
# every run's hash of decisions, phases, saved records and alert resends.
#
#   bash tools/close-diff/run.sh [runs] [base-revision]
#
# Exit 0 when every run is identical.
set -euo pipefail

RUNS="${1:-20000}"
BASE="${2:-origin/main}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

ARDUINOJSON="$(ls -d "$ROOT"/.pio/libdeps/native/ArduinoJson/src 2>/dev/null || true)"
if [ -z "$ARDUINOJSON" ]; then
    echo "close-diff: run 'pio test -e native' once first (ArduinoJson)" >&2
    exit 2
fi

mkdir -p "$WORK/base"
git -C "$ROOT" archive "$BASE" lib | tar -x -C "$WORK/base"

LIBS="AlertLogic CloseSequenceLogic ConfigModel DaylightLogic DriverLogic NetAddress RoofIoLogic RoofLogic SafetyInputLogic NinaLogic"

STRIP="-Wl,--gc-sections"
[ "$(uname)" = "Darwin" ] && STRIP="-Wl,-dead_strip"

build() {
    local tree="$1" out="$2" includes="" sources=""
    for dir in "$tree"/lib/*/include; do
        includes="$includes -I$dir"
    done
    for lib in $LIBS; do
        [ -d "$tree/lib/$lib" ] || continue
        includes="$includes -I$tree/lib/$lib/src"
        sources="$sources $(ls "$tree/lib/$lib/src/"*.cpp)"
    done
    # shellcheck disable=SC2086
    c++ -std=gnu++17 -O1 -w $includes -I"$ARDUINOJSON" "$ROOT/tools/close-diff/driver.cpp" $sources -ffunction-sections -fdata-sections $STRIP -o "$out"
}

build "$WORK/base" "$WORK/base-driver"
build "$ROOT" "$WORK/this-driver"
"$WORK/base-driver" "$RUNS" > "$WORK/base.txt"
"$WORK/this-driver" "$RUNS" > "$WORK/this.txt"
if ! diff -q "$WORK/base.txt" "$WORK/this.txt" > /dev/null; then
    echo "close-diff: runs differ from $BASE:" >&2
    diff "$WORK/base.txt" "$WORK/this.txt" | head -20 >&2
    exit 1
fi
echo "close-diff: $RUNS runs identical to $BASE"
