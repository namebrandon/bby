#!/bin/bash
# Cross-compile for Windows using MinGW
set -e
BUILD_TYPE=${1:-Release}
mkdir -p build/windows
cd build/windows

# Create toolchain file if it doesn't exist
if [ ! -f "../../toolchain-mingw64.cmake" ]; then
    cat > ../../toolchain-mingw64.cmake << 'TOOLCHAIN'
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++ -static")
TOOLCHAIN
fi

cmake ../.. \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=../../toolchain-mingw64.cmake \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DCMAKE_CXX_STANDARD=17 \
    -DCMAKE_CXX_FLAGS="-march=haswell -mbmi2 -mpopcnt"
ninja
echo "✓ Windows build complete: build/windows/"
