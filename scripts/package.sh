#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# package.sh – Create a self-contained tarball of qctop for distribution.
#
# Run after build.sh:
#   ./scripts/build.sh && ./scripts/package.sh
#
# Output: qctop-<arch>.tar.gz  in the project root.
# Team members only need to copy the tarball to the target device and run:
#   tar xzf qctop-<arch>.tar.gz
#   ./qctop-<arch>/qctop -h
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BINARY="${BUILD_DIR}/qctop"

if [[ ! -f "${BINARY}" ]]; then
    echo "ERROR: ${BINARY} not found.  Run ./scripts/build.sh first." >&2
    exit 1
fi

ARCH="$(uname -m)"
PKG_NAME="qctop-${ARCH}"
PKG_DIR="${BUILD_DIR}/${PKG_NAME}"

rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}"

# Copy binary
cp "${BINARY}" "${PKG_DIR}/"

# Copy a minimal README
cat > "${PKG_DIR}/README.txt" <<'EOF'
qctop – Qualcomm SoC performance profiler
==========================================

Usage:
  ./qctop                   All metrics, 1 s interval (loops until Ctrl-C)
  ./qctop -l                Explicit continuous loop (same as default)
  ./qctop -1                Single snapshot then exit
  ./qctop -i 500            All metrics, refresh every 500 ms
  ./qctop -n 10             All metrics, 10 snapshots then exit
  ./qctop -h                Full help

Subsystem filters (specify one or more to restrict output):
  ./qctop --cpu             Per-core CPU utilisation + cluster freq only
  ./qctop --gpu             GPU util/clock/memory only
  ./qctop --mem             RAM/PSS/ION/FastRPC only
  ./qctop --ddr             DDR clock only
  ./qctop --storage         Storage clock/I/O/filesystem only
  ./qctop --thermal         All thermal zones (individual columns)
  ./qctop --net             Per-interface RX/TX bytes only
  ./qctop --npu             NPU/Hexagon metrics only (if compiled in)

Multiple filters can be combined:
  ./qctop --cpu --gpu -i 500        CPU + GPU at 500 ms
  ./qctop --thermal -n 60           60-sample thermal log
  ./qctop --cpu --thermal -i 200    CPU + thermal at 200 ms

Run `./qctop -h` and check the NPU line to see if they are enabled.

If NPU metrics are needed and were not compiled in, install qcnpuperf
and rebuild from source:
  git clone <repo>
  cd qctop && ./scripts/build.sh
EOF

# Create the tarball
TARBALL="${ROOT_DIR}/${PKG_NAME}.tar.gz"
tar -czf "${TARBALL}" -C "${BUILD_DIR}" "${PKG_NAME}"

echo "✓ Package created: ${TARBALL}"
echo "  Copy to device and run:"
echo "    tar xzf ${PKG_NAME}.tar.gz && ./${PKG_NAME}/qctop -h"
