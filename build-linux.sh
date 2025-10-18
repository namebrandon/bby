#!/bin/bash
# Build for Linux (native x86-64 with BMI2)
set -e
BUILD_TYPE=${1:-Release}
mkdir -p build/linux
cd build/linux
cmake ../.. \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DCMAKE_CXX_STANDARD=17 \
    -DCMAKE_CXX_FLAGS="-march=haswell -mbmi2 -mpopcnt"
ninja
echo "✓ Linux build complete: build/linux/"
