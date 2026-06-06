#!/usr/bin/env bash
# Release build of RyBench (-O3). Usage: scripts/build.sh [target]
#   scripts/build.sh                  # configure + build everything
#   scripts/build.sh rybench-iceoryx2 # build a single target (faster)
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR=build_release
TARGET="${1:-}"

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

if [[ -n "$TARGET" ]]; then
    cmake --build "$BUILD_DIR" --target "$TARGET" -j"$(nproc)"
else
    cmake --build "$BUILD_DIR" -j"$(nproc)"
fi

echo "Done. Binaries in $BUILD_DIR/bin/"
