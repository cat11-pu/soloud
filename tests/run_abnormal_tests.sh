#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/tests/build"
CXX="${CXX:-g++}"

mkdir -p "$BUILD_DIR"

"$CXX" \
	-std=c++11 \
	-O1 \
	-g \
	-fno-omit-frame-pointer \
	-DDISABLE_SIMD \
	-fsanitize=address,undefined \
	-DWITH_NULL \
	-I"$ROOT/include" \
	"$ROOT/tests/audio_engine_abnormal_test.cpp" \
	"$ROOT"/src/core/*.cpp \
	"$ROOT/src/backend/null/soloud_null.cpp" \
	"$ROOT/src/audiosource/wav/soloud_wav.cpp" \
	"$ROOT/src/audiosource/wav/dr_impl.cpp" \
	"$ROOT/src/audiosource/wav/stb_vorbis.c" \
	-lpthread \
	-lm \
	-o "$BUILD_DIR/audio_engine_abnormal_test_asan"

ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" \
UBSAN_OPTIONS="halt_on_error=1" \
	"$BUILD_DIR/audio_engine_abnormal_test_asan"
