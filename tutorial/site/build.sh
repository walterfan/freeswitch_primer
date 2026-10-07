#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Release}"

conan install "${SCRIPT_DIR}" \
  --output-folder="${SCRIPT_DIR}/build" \
  --build=missing \
  --settings="build_type=${BUILD_TYPE}"

cmake -S "${SCRIPT_DIR}" -B "${SCRIPT_DIR}/build" \
  -DCMAKE_TOOLCHAIN_FILE="${SCRIPT_DIR}/build/build/${BUILD_TYPE}/generators/conan_toolchain.cmake" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DBUILD_TESTS=ON

cmake --build "${SCRIPT_DIR}/build" --parallel
ctest --test-dir "${SCRIPT_DIR}/build" --output-on-failure

