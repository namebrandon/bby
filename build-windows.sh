#!/usr/bin/env bash
# Build the statically linked Windows engine from an MSYS2 UCRT64 shell.
set -euo pipefail

BUILD_TYPE=${1:-Release}
BUILD_DIR="build/windows"
ENGINE_NAME="bby_win_bmi2"

if ! command -v pacman >/dev/null 2>&1; then
  echo "error: pacman not found. Run this script inside an MSYS2 UCRT64 shell." >&2
  exit 1
fi

REQUIRED_PKGS=(
  mingw-w64-ucrt-x86_64-toolchain
  mingw-w64-ucrt-x86_64-cmake
  mingw-w64-ucrt-x86_64-ninja
)

missing=()
for pkg in "${REQUIRED_PKGS[@]}"; do
  if ! pacman -Q "$pkg" &>/dev/null; then
    missing+=("$pkg")
  fi
done

if ((${#missing[@]} > 0)); then
  echo "Installing required packages: ${missing[*]}"
  pacman -S --needed "${missing[@]}"
fi

cmake -S . -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread" \
  -DCMAKE_CXX_FLAGS="-march=haswell -mbmi2 -mpopcnt" \
  -DBBY_OUTPUT_NAME="$ENGINE_NAME"

cmake --build "$BUILD_DIR" --target bby

echo "✓ Windows build complete: $BUILD_DIR/${ENGINE_NAME}.exe"
