#!/bin/sh
# SemiclipML Linux build (i386 .so for GoldSrc / Sven Co-op dedicated server)
# Requires: build-essential gcc-multilib g++-multilib cmake (>=3.15) git
set -e
cd "$(dirname "$0")"

if [ ! -f thirdparty/metamod-p/metamod/meta_api.h ]; then
    [ -d .git ] || { echo "[ERR] no .git and no thirdparty/metamod-p"; exit 1; }
    echo "[INFO] thirdparty/metamod-p is empty, initializing submodules..."
    git submodule update --init --recursive
fi

rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

SO=$(find build -name '*SemiclipML.so' | head -n1)
[ -n "$SO" ] || { echo "[ERR] .so not found"; exit 1; }
file "$SO" | grep -q "ELF 32-bit" || { echo "[ERR] not i386: $(file "$SO")"; exit 1; }

EXTRA=$(nm -D --defined-only "$SO" | awk '$2=="T"{print $3}' |
  grep -vxE 'Meta_Query|Meta_Attach|Meta_Detach|GiveFnptrsToDll|GetEntityAPI2|GetEntityAPI2_Post|GetNewDLLFunctions|GetEngineFunctions|GetEngineFunctions_Post' || true)
if [ -n "$EXTRA" ]; then
    echo "[WARN] unexpected exports:"
    echo "$EXTRA" | head -n 5
fi

if nm -D --undefined-only "$SO" | grep -q 'GLIBCXX\|CXXABI'; then
    echo "[WARN] depends on system libstdc++ (static link did not apply)"
fi
echo "[INFO] max glibc required: $(objdump -T "$SO" | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -n1)"

mkdir -p bin
cp "$SO" bin/SemiclipML.so
echo "[DONE] bin/SemiclipML.so"
file bin/SemiclipML.so
