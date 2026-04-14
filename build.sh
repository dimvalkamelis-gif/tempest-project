#!/usr/bin/env bash
set -euo pipefail

# Tempest build script (Linux/macOS)
#
# Usage:
#   ./build.sh
#   ./build.sh Release
#   ./build.sh Debug
#   ./build.sh Release -DTEMPEST_ENABLE_RAG=ON -DTEMPEST_ENABLE_TELEGRAM=ON -DTEMPEST_ENABLE_LLAMA=ON
#   ./build.sh --pause
#   ./build.sh Release --pause

PAUSE=0
if [[ "${*: -1}" == "--pause" ]]; then
  PAUSE=1
  set -- "${@:1:$(($#-1))}"
fi

BUILD_TYPE="${1:-Release}"
if [[ $# -gt 0 ]]; then shift; fi

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SRC_DIR}/build"

echo "[tempest] Configuring (type=${BUILD_TYPE})..."
cmake -S "${SRC_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" "$@"

echo "[tempest] Building..."
cmake --build "${BUILD_DIR}" -j

echo "[tempest] Done. Run:"
echo "  ${BUILD_DIR}/tempest"

if [[ "${PAUSE}" == "1" ]]; then
  echo
  read -r -p "[tempest] Press Enter to close..." _
fi
