#!/bin/sh
# SemiclipML Linux build (i386 .so for GoldSrc / Sven Co-op dedicated server)
# Requires: build-essential gcc-multilib g++-multilib cmake git
set -e
cd "$(dirname "$0")"

if [ ! -f thirdparty/metamod-p/metamod/meta_api.h ]; then
    echo "[INFO] thirdparty/metamod-p is empty, initializing submodules..."
    git submodule update --init --recursive
fi

rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j"$(nproc)"

mkdir -p bin
cp build/SemiclipML.so bin/
echo "[DONE] bin/SemiclipML.so"
file bin/SemiclipML.so
