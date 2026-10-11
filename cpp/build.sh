#!/usr/bin/env bash
# Build the C++ port of opencam (Qt6 Widgets + OpenCV 5).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
cd build
qmake6 ../opencam.pro
make -j"$(nproc)"
echo "OK: $(pwd)/opencam"
