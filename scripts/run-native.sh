#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONFIGURE_PRESET="dev-macos-debug"
BUILD_PRESET="build-dev-macos-debug"
BUILD_DIR="${REPO_ROOT}/build/${CONFIGURE_PRESET}"
APP_BINARY="${BUILD_DIR}/apps/desktop/PhotoEditor.app/Contents/MacOS/PhotoEditor"
NATIVE_LOG_STYLE="${PHOTO_EDITOR_NATIVE_LOG_STYLE:-pretty}"
LOG_COLOR_MODE="${PHOTO_EDITOR_LOG_COLOR:-auto}"

color_enabled=0
if [ "${LOG_COLOR_MODE}" = "always" ]; then
  color_enabled=1
elif [ "${LOG_COLOR_MODE}" != "never" ] &&
     [ -z "${NO_COLOR:-}" ] &&
     [ -t 1 ] &&
     [ "${TERM:-}" != "dumb" ]; then
  color_enabled=1
fi

if [ "${NATIVE_LOG_STYLE}" = "plain" ]; then
  color_enabled=0
fi

if [ "${color_enabled}" -eq 1 ]; then
  RESET=$'\033[0m'
  BOLD=$'\033[1m'
  DIM=$'\033[2m'
  CYAN=$'\033[36m'
  BLUE=$'\033[34m'
  MAGENTA=$'\033[35m'
  GREEN=$'\033[32m'
  YELLOW=$'\033[33m'
  RED=$'\033[31;1m'
else
  RESET=""
  BOLD=""
  DIM=""
  CYAN=""
  BLUE=""
  MAGENTA=""
  GREEN=""
  YELLOW=""
  RED=""
fi

started_at="$(date +%s)"
step_index=0

timestamp() {
  date +"%H:%M:%S"
}

duration_since() {
  local start="$1"
  local now
  now="$(date +%s)"
  printf '%ss' "$((now - start))"
}

log_line() {
  local color="$1"
  local label="$2"
  shift 2
  printf '%s%s[%s] %-9s%s %s\n' "${color}" "${BOLD}" "$(timestamp)" "${label}" "${RESET}" "$*"
}

banner() {
  if [ "${NATIVE_LOG_STYLE}" = "plain" ]; then
    printf 'PhotoEditor native run\n'
    return
  fi
  printf '\n%s%s\n' "${CYAN}" "============================================================"
  printf '%s  PhotoEditor native run%s\n' "${BOLD}" "${RESET}${CYAN}"
  printf '%s  preset:%s %s  %sbuild:%s %s\n' \
    "${DIM}" "${RESET}${CYAN}" "${CONFIGURE_PRESET}" "${DIM}" "${RESET}${CYAN}" "${BUILD_PRESET}"
  printf '%s%s\n\n' "============================================================" "${RESET}"
}

step_start() {
  step_index=$((step_index + 1))
  if [ "${NATIVE_LOG_STYLE}" != "plain" ]; then
    printf '\n'
  fi
  log_line "${BLUE}" "step ${step_index}" "$*"
}

step_done() {
  local start="$1"
  shift
  log_line "${GREEN}" "done" "$* ${DIM}($(duration_since "${start}"))${RESET}"
}

command_preview() {
  printf '%s  $ %s%s\n' "${DIM}" "$*" "${RESET}"
}

run_step() {
  local title="$1"
  shift
  local step_started
  local status
  step_started="$(date +%s)"
  step_start "${title}"
  command_preview "$*"
  if "$@"; then
    step_done "${step_started}" "${title}"
  else
    status=$?
    log_line "${RED}" "failed" "${title} ${DIM}($(duration_since "${step_started}"))${RESET}"
    return "${status}"
  fi
}

fail() {
  log_line "${RED}" "error" "$*" >&2
  exit 1
}

note() {
  log_line "${CYAN}" "info" "$*"
}

warn() {
  log_line "${YELLOW}" "warn" "$*" >&2
}

if ! command -v cmake >/dev/null 2>&1; then
  fail "cmake is required. Install it with: brew install cmake ninja rust qt"
fi

if ! command -v ninja >/dev/null 2>&1; then
  fail "ninja is required. Install it with: brew install ninja"
fi

if ! command -v cargo >/dev/null 2>&1; then
  fail "cargo is required. Install it with: brew install rust"
fi

banner

if [ -z "${CMAKE_PREFIX_PATH:-}" ] && command -v brew >/dev/null 2>&1; then
  QT_PREFIX="$(brew --prefix qt 2>/dev/null || true)"
  if [ -n "${QT_PREFIX}" ]; then
    export CMAKE_PREFIX_PATH="${QT_PREFIX}"
    export PATH="${QT_PREFIX}/bin:${PATH}"
    note "Qt from Homebrew: ${QT_PREFIX}"
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

note "Runtime data: ${PHOTO_EDITOR_APP_DATA_DIR}"
note "Logs: ${PHOTO_EDITOR_LOG_DIR}"
run_step "Rust catalog binaries" \
  cargo build --manifest-path rust/Cargo.toml -p photo-catalog --bins
if [ -n "${PHOTO_EDITOR_DATABASE_URL:-}" ]; then
  run_step "Catalog migrations" \
    "${REPO_ROOT}/rust/target/debug/catalog_migrate"
else
  run_step "Local catalog database" \
    "${REPO_ROOT}/rust/target/debug/catalog_local_db" ensure-running
fi
run_step "Desktop configure" \
  cmake --preset "${CONFIGURE_PRESET}" \
  -DPHOTO_EDITOR_RUST_BINARY_DIR="${REPO_ROOT}/rust/target/debug"
run_step "Desktop build" \
  cmake --build --preset "${BUILD_PRESET}"

EXISTING_APP_PIDS="$(pgrep -f "${BUILD_DIR}/apps/desktop/(photo_editor_app|PhotoEditor.app/Contents/MacOS/PhotoEditor)" || true)"
if [ -n "${EXISTING_APP_PIDS}" ]; then
  warn "Stopping existing PhotoEditor instance before launch"
  while IFS= read -r pid; do
    if [ -n "${pid}" ]; then
      kill "${pid}" 2>/dev/null || true
    fi
  done <<< "${EXISTING_APP_PIDS}"
  sleep 1
fi

note "App binary: ${APP_BINARY}"
note "Build pipeline ready in $(duration_since "${started_at}")"
log_line "${MAGENTA}" "launch" "PhotoEditor is taking over this terminal"
exec "${APP_BINARY}"
