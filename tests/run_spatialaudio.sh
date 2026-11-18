#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CXX=${CXX:-g++}
OUT=${TMPDIR:-/tmp}/soloud_spatialaudio_test

"$CXX" -std=c++11 -O2 -Wall -Wextra \
	-I"$ROOT/include" \
	"$ROOT/tests/spatialaudio_test.cpp" \
	"$ROOT/src/core/soloud_spatialaudio.cpp" \
	-o "$OUT"

"$OUT"
"$OUT" --dump-panning "$ROOT/artifacts/spatial_audio_panning.csv"
