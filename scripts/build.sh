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
LOG_FILE="${BUILD_DIR}/build.log"

# Optional: -DSTATIC_BUILD=OFF to produce a dynamically linked binary.
STATIC="${3:-ON}"
CLEAN_BUILD="${QCTOP_CLEAN_BUILD:-0}"

mkdir -p "${BUILD_DIR}"
: > "${LOG_FILE}"

log() {
    printf '%s\n' "$*" | tee -a "${LOG_FILE}"
}

run() {
    log "$*"
    "$@" 2>&1 | tee -a "${LOG_FILE}"
}

trap 'log "Build failed. See ${LOG_FILE}"' ERR

log "──────────────────────────────────────────────"
log "  qctop build"
log "  Build type  : ${BUILD_TYPE}"
log "  Static      : ${STATIC}"
log "  Clean build : ${CLEAN_BUILD}"
log "  Source dir  : ${ROOT_DIR}"
log "  Build dir   : ${BUILD_DIR}"
log "  Log file    : ${LOG_FILE}"
[[ -n "${TOOLCHAIN}" ]] && log "  Toolchain   : ${TOOLCHAIN}"
log "──────────────────────────────────────────────"

# For a static build on Debian/Ubuntu the following packages are needed:
#   sudo apt install libc6-dev gcc musl-tools   (native)
# For cross-compile with musl, point CMAKE_SYSROOT at a musl sysroot.
# Set STATIC=OFF if your system only has shared libraries.

CMAKE_EXTRA_ARGS=("-DSTATIC_BUILD=${STATIC}")
[[ -n "${TOOLCHAIN}" ]] && CMAKE_EXTRA_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN}")

run cmake -S "${ROOT_DIR}" \
    -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    "${CMAKE_EXTRA_ARGS[@]}"

BUILD_CMD=(cmake --build "${BUILD_DIR}" --parallel "$(nproc)")
if [[ "${CLEAN_BUILD}" == "1" || "${CLEAN_BUILD}" == "true" || "${CLEAN_BUILD}" == "yes" ]]; then
    BUILD_CMD+=(--clean-first)
fi
run "${BUILD_CMD[@]}"

log ""
log "✓ Build complete: ${BUILD_DIR}/qctop"

# Show link type
if file "${BUILD_DIR}/qctop" 2>/dev/null | grep -q "statically linked"; then
    log "  Link type   : STATIC  (no runtime .so dependencies)"
else
    log "  Link type   : DYNAMIC"
    log "  Dependencies:"
    ldd "${BUILD_DIR}/qctop" 2>/dev/null | sed 's/^/    /' | tee -a "${LOG_FILE}" || true
fi

log "Build log saved to ${LOG_FILE}"
