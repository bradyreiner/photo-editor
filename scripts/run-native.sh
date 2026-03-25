#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/dev"

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake is required. Install it with: brew install cmake ninja rust qt"
  exit 1
fi

if ! command -v ninja >/dev/null 2>&1; then
  echo "ninja is required. Install it with: brew install ninja"
  exit 1
fi

if ! command -v cargo >/dev/null 2>&1; then
  echo "cargo is required. Install it with: brew install rust"
  exit 1
fi

if [ -z "${CMAKE_PREFIX_PATH:-}" ] && command -v brew >/dev/null 2>&1; then
  QT_PREFIX="$(brew --prefix qt 2>/dev/null || true)"
  if [ -n "${QT_PREFIX}" ]; then
    export CMAKE_PREFIX_PATH="${QT_PREFIX}"
    export PATH="${QT_PREFIX}/bin:${PATH}"
  fi
fi

cd "${REPO_ROOT}"

echo "Building Rust catalog binaries..."
cargo build --manifest-path rust/Cargo.toml -p photo-catalog --bins
echo "Configuring desktop build..."
cmake -S . -B "${BUILD_DIR}" -G Ninja -DPHOTO_EDITOR_BUILD_DESKTOP=ON
echo "Building desktop app..."
cmake --build "${BUILD_DIR}"

echo "Launching PhotoEditor..."
echo "The terminal will stay attached while the app is open."
exec "${BUILD_DIR}/apps/desktop/photo_editor_app"
