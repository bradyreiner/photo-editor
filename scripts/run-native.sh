#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/dev"
APP_BINARY="${BUILD_DIR}/apps/desktop/PhotoEditor.app/Contents/MacOS/PhotoEditor"
EXPECTED_CMAKE_GENERATOR="Ninja"

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

if [ -z "${PHOTO_EDITOR_APP_DATA_DIR:-}" ]; then
  PHOTO_EDITOR_APP_DATA_DIR="${HOME}/Library/Application Support/PhotoEditor"
fi
if [ -z "${PHOTO_EDITOR_LOG_DIR:-}" ]; then
  PHOTO_EDITOR_LOG_DIR="${PHOTO_EDITOR_APP_DATA_DIR}/logs"
fi
if [ -z "${PHOTO_EDITOR_BACKUP_DIR:-}" ]; then
  PHOTO_EDITOR_BACKUP_DIR="${PHOTO_EDITOR_APP_DATA_DIR}/backups"
fi

export PHOTO_EDITOR_APP_DATA_DIR
export PHOTO_EDITOR_LOG_DIR
export PHOTO_EDITOR_BACKUP_DIR

mkdir -p "${PHOTO_EDITOR_APP_DATA_DIR}" "${PHOTO_EDITOR_LOG_DIR}" "${PHOTO_EDITOR_BACKUP_DIR}"

if [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then
  CURRENT_CMAKE_GENERATOR="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt" | head -n 1)"
  if [ -n "${CURRENT_CMAKE_GENERATOR}" ] && [ "${CURRENT_CMAKE_GENERATOR}" != "${EXPECTED_CMAKE_GENERATOR}" ]; then
    echo "Resetting ${BUILD_DIR} CMake metadata because generator '${CURRENT_CMAKE_GENERATOR}' does not match '${EXPECTED_CMAKE_GENERATOR}'..."
    rm -rf \
      "${BUILD_DIR}/CMakeCache.txt" \
      "${BUILD_DIR}/CMakeFiles" \
      "${BUILD_DIR}/Makefile" \
      "${BUILD_DIR}/build.ninja" \
      "${BUILD_DIR}/cmake_install.cmake" \
      "${BUILD_DIR}/.ninja_deps" \
      "${BUILD_DIR}/.ninja_log"
  fi
fi

echo "Building Rust catalog binaries..."
cargo build --manifest-path rust/Cargo.toml -p photo-catalog --bins
if [ -n "${PHOTO_EDITOR_DATABASE_URL:-}" ]; then
  echo "Applying catalog migrations against configured database..."
  "${REPO_ROOT}/rust/target/debug/catalog_migrate"
else
  echo "Bootstrapping local catalog database..."
  "${REPO_ROOT}/rust/target/debug/catalog_local_db" ensure-running
fi
echo "Configuring desktop build..."
cmake -S . -B "${BUILD_DIR}" -G "${EXPECTED_CMAKE_GENERATOR}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPHOTO_EDITOR_BUILD_DESKTOP=ON \
  -DPHOTO_EDITOR_RUST_BINARY_DIR="${REPO_ROOT}/rust/target/debug"
echo "Building desktop app..."
cmake --build "${BUILD_DIR}" --target clean
cmake --build "${BUILD_DIR}"

EXISTING_APP_PIDS="$(pgrep -f "${BUILD_DIR}/apps/desktop/(photo_editor_app|PhotoEditor.app/Contents/MacOS/PhotoEditor)" || true)"
if [ -n "${EXISTING_APP_PIDS}" ]; then
  echo "Stopping existing PhotoEditor instance..."
  while IFS= read -r pid; do
    if [ -n "${pid}" ]; then
      kill "${pid}" 2>/dev/null || true
    fi
  done <<< "${EXISTING_APP_PIDS}"
  sleep 1
fi

echo "Launching PhotoEditor..."
echo "The terminal will stay attached while the app is open."
exec "${APP_BINARY}"
