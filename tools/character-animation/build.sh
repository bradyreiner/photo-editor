#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${REPO_ROOT}/scripts/build-lock.sh"
acquire_build_lock "${REPO_ROOT}/build/care-comes-back"
cmake -S "${SCRIPT_DIR}" -B "${REPO_ROOT}/build/care-comes-back" -DCMAKE_BUILD_TYPE=Release
cmake --build "${REPO_ROOT}/build/care-comes-back" -j 2
ctest --test-dir "${REPO_ROOT}/build/care-comes-back" --output-on-failure
