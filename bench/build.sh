#!/bin/bash
# Builds the mix benchmark. Usage: build.sh <output-binary>
set -e
cd "$(dirname "$0")/.."
g++ -O2 -DNDEBUG -DWITH_NULL -Iinclude \
	bench/mixbench.cpp \
	src/core/*.cpp \
	src/audiosource/wav/soloud_wav.cpp \
	src/audiosource/wav/soloud_wavstream.cpp \
	src/audiosource/wav/dr_impl.cpp \
	src/audiosource/wav/stb_vorbis.c \
	src/backend/null/soloud_null.cpp \
	-lpthread -lm -o "$1"
