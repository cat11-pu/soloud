#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT/tests/stability/build"
CXX="${CXX:-g++}"

mkdir -p "$BUILD_DIR"

"$CXX" \
	-std=c++11 \
	-O1 \
	-g \
	-fno-omit-frame-pointer \
	-fsanitize=address,undefined \
	-DWITH_NOSOUND \
	-I"$ROOT/include" \
	"$ROOT/tests/stability/stability_test.cpp" \
	"$ROOT"/src/core/*.cpp \
	"$ROOT/src/backend/nosound/soloud_nosound.cpp" \
	"$ROOT/src/audiosource/noise/soloud_noise.cpp" \
	-lpthread \
	-lm \
	-o "$BUILD_DIR/stability_test_asan"

ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" \
UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
	"$BUILD_DIR/stability_test_asan"

"$CXX" \
	-std=c++11 \
	-O1 \
	-g \
	-fno-omit-frame-pointer \
	-fsanitize=address,undefined \
	-fno-sanitize=vptr \
	-DWITH_NULL \
	-I"$ROOT/include" \
	-ffunction-sections \
	-fdata-sections \
	"$ROOT/tests/stability/capi_destroy_test.cpp" \
	"$ROOT/src/c_api/soloud_c.cpp" \
	"$ROOT"/src/core/*.cpp \
	"$ROOT/src/backend/null/soloud_null.cpp" \
	-Wl,--gc-sections \
	-lpthread \
	-lm \
	-o "$BUILD_DIR/capi_destroy_test_asan"

ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" \
UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
	"$BUILD_DIR/capi_destroy_test_asan"
