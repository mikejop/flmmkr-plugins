#!/bin/bash
# Builds a STATIC, universal (arm64 + x86_64) OpenColorIO with deployment target macOS 13, so the plugin
# has no Homebrew dependency and loads on other machines. Output: third_party/ocio-static
# Needs network (clones OCIO, downloads its third-party deps). Takes several minutes.
set -euo pipefail
# CMake 4 rejects the old cmake_minimum_required of some OCIO third-party deps.
export CMAKE_POLICY_VERSION_MINIMUM=3.5
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="${OCIO_VERSION:-v2.4.2}"
SRC="$ROOT/third_party/OpenColorIO-src"
OUT="$ROOT/third_party/ocio-static"
[ -d "$SRC" ] || git clone --depth 1 --branch "$VER" https://github.com/AcademySoftwareFoundation/OpenColorIO.git "$SRC"
cmake -S "$SRC" -B "$SRC/build" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DBUILD_SHARED_LIBS=OFF -DOCIO_BUILD_APPS=OFF -DOCIO_BUILD_PYTHON=OFF -DOCIO_BUILD_OPENFX=OFF \
  -DOCIO_BUILD_TESTS=OFF -DOCIO_BUILD_GPU_TESTS=OFF -DOCIO_BUILD_DOCS=OFF \
  -DOCIO_INSTALL_EXT_PACKAGES=MISSING -DCMAKE_IGNORE_PREFIX_PATH="/opt/homebrew;/usr/local" -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_INSTALL_PREFIX="$OUT"
cmake --build "$SRC/build" --config Release -j"$(sysctl -n hw.ncpu)"
cmake --install "$SRC/build"
cp -R "$SRC/build/ext/dist/." "$OUT/"   # static deps (Imath, yaml-cpp, minizip-ng, pystring)
echo "OCIO static installed in $OUT"
