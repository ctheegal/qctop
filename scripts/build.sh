#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# build.sh – Build qctop for the local machine (or cross-compile).
#
# Usage:
#   ./scripts/build.sh                  # native build, Release
#   ./scripts/build.sh Debug            # native debug build
#   ./scripts/build.sh Release <toolchain-file>  # cross-compile
#
# After building, the binary is at:  build/qctop
# To install system-wide:            sudo cmake --install build
# To create a relocatable tarball:   ./scripts/build.sh && ./scripts/package.sh
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_TYPE="${1:-Release}"
TOOLCHAIN="${2:-}"
BUILD_DIR="${ROOT_DIR}/build"

# Optional: -DSTATIC_BUILD=OFF to produce a dynamically linked binary.
STATIC="${3:-ON}"

echo "──────────────────────────────────────────────"
echo "  qctop build"
echo "  Build type  : ${BUILD_TYPE}"
echo "  Static      : ${STATIC}"
echo "  Source dir  : ${ROOT_DIR}"
echo "  Build dir   : ${BUILD_DIR}"
[[ -n "${TOOLCHAIN}" ]] && echo "  Toolchain   : ${TOOLCHAIN}"
echo "──────────────────────────────────────────────"

# For a static build on Debian/Ubuntu the following packages are needed:
#   sudo apt install libc6-dev gcc musl-tools   (native)
# For cross-compile with musl, point CMAKE_SYSROOT at a musl sysroot.
# Set STATIC=OFF if your system only has shared libraries.

CMAKE_EXTRA_ARGS=("-DSTATIC_BUILD=${STATIC}")
[[ -n "${TOOLCHAIN}" ]] && CMAKE_EXTRA_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN}")

cmake -S "${ROOT_DIR}" \
      -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      "${CMAKE_EXTRA_ARGS[@]}"

cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

echo ""
echo "✓ Build complete: ${BUILD_DIR}/qctop"

# Show link type
if file "${BUILD_DIR}/qctop" 2>/dev/null | grep -q "statically linked"; then
    echo "  Link type   : STATIC  (no runtime .so dependencies)"
else
    echo "  Link type   : DYNAMIC"
    echo "  Dependencies:"
    ldd "${BUILD_DIR}/qctop" 2>/dev/null | sed 's/^/    /' || true
fi
