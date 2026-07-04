#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release -j"$(nproc)"

if [[ -f "${BUILD_DIR}/platform/linux/UmbraNote" ]]; then
  cp -f "${BUILD_DIR}/platform/linux/UmbraNote" "${ROOT}/UmbraNote"
  echo "Build successful: ${ROOT}/UmbraNote"
elif [[ -f "${BUILD_DIR}/UmbraNote" ]]; then
  cp -f "${BUILD_DIR}/UmbraNote" "${ROOT}/UmbraNote"
  echo "Build successful: ${ROOT}/UmbraNote"
else
  echo "Build finished, but UmbraNote binary was not found in ${BUILD_DIR}." >&2
  exit 1
fi