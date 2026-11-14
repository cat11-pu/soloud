#!/bin/bash
# Build mixbench in two configs:
#   build/mixbench_simd   - default build (SSE intrinsics on x86_64)
#   build/mixbench_scalar - DISABLE_SIMD, same code path as ARM/mobile fallback
set -e
cd "$(dirname "$0")/.."

SRC="src/core/*.cpp src/audiosource/wav/soloud_wav.cpp src/audiosource/wav/dr_impl.cpp src/audiosource/wav/stb_vorbis.c src/backend/null/soloud_null.cpp bench/mixbench.cpp"
INC="-Iinclude"
DEF="-DWITH_NULL"
FLAGS="-O2 -fno-fast-math"

mkdir -p bench/build

echo "== building simd config =="
g++ $FLAGS $DEF $INC $SRC -o bench/build/mixbench_simd -lpthread

echo "== building scalar (mobile-like) config =="
g++ $FLAGS $DEF -DDISABLE_SIMD $INC $SRC -o bench/build/mixbench_scalar -lpthread

echo "done"
