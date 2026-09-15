#!/bin/sh
# Cross-build Vd.dll with MinGW (x64) from Linux/macOS.
# Output -> build/Vd.dll (single file, runtime statically linked).
set -eu
mkdir -p build
x86_64-w64-mingw32-g++ -O2 -Wall -Wextra -shared -static-libgcc -static-libstdc++ -static \
  -o build/Vd.dll src/vd.cpp -I src -lole32 -luser32
echo "built: build/Vd.dll"
