#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONFIGURE_PRESET="release-macos"
BUILD_ENABLED=1
BUILD_ONLY=0
FORCE_CONFIGURE=0
BUILD_JOBS=""

usage() {
  cat <<'HELP'
Usage: ./scripts/run-native.sh [options] [-- app arguments...]

Build only Looksy and its dependencies, prepare PostgreSQL, and launch.
Existing builds are incremental; tests and benchmarks are built separately.

  --release       Use the optimized release-macos build (default)
  --debug         Use dev-macos-debug for debugger sessions
  --no-build      Launch the existing app without Cargo or CMake
  --build-only    Build the app without starting PostgreSQL or the app
  --configure     Refresh CMake configuration before building
  --jobs N        Limit concurrent build jobs (positive integer)
  -h, --help      Show this help

Examples:
  ./scripts/run-native.sh
  ./scripts/run-native.sh --no-build
  ./scripts/run-native.sh --release --jobs 4
HELP
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --release) CONFIGURE_PRESET="release-macos" ;;
    --debug) CONFIGURE_PRESET="dev-macos-debug" ;;
    --no-build) BUILD_ENABLED=0 ;;
    --build-only) BUILD_ONLY=1 ;;
    --configure) FORCE_CONFIGURE=1 ;;
    --jobs)
      shift
      case "${1:-}" in
        ''|*[!0-9]*|0*) printf 'Error: --jobs requires a positive integer.\n' >&2; exit 2 ;;
      esac
      BUILD_JOBS="$1"
      ;;
    -h|--help) usage; exit 0 ;;
    --) shift; break ;;
    *) printf 'Unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done
if [ "${BUILD_ENABLED}" -eq 0 ] &&
   { [ "${BUILD_ONLY}" -eq 1 ] || [ "${FORCE_CONFIGURE}" -eq 1 ] || [ -n "${BUILD_JOBS}" ]; }; then
  printf 'Error: --no-build cannot be combined with build options.\n' >&2
  exit 2
fi
BUILD_PRESET="build-${CONFIGURE_PRESET}"
BUILD_DIR="${REPO_ROOT}/build/${CONFIGURE_PRESET}"
APP_BINARY="${BUILD_DIR}/apps/desktop/Looksy.app/Contents/MacOS/Looksy"
RUST_BIN_DIR="${REPO_ROOT}/rust/target/debug"
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

if [ "${BUILD_ENABLED}" -eq 1 ]; then
  for tool in cmake ninja cargo; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
      fail "${tool} is required to build. Install build tools with: brew install cmake ninja rust qt"
    fi
  done
fi

banner

if [ "${BUILD_ENABLED}" -eq 1 ] && [ -z "${CMAKE_PREFIX_PATH:-}" ] && command -v brew >/dev/null 2>&1; then
  QT_PREFIX="$(brew --prefix qt 2>/dev/null || true)"
  if [ -n "${QT_PREFIX}" ]; then
    export CMAKE_PREFIX_PATH="${QT_PREFIX}"
    export PATH="${QT_PREFIX}/bin:${PATH}"
    note "Qt from Homebrew: ${QT_PREFIX}"
  fi
fi

cd "${REPO_ROOT}"

if [ "${BUILD_ENABLED}" -eq 1 ]; then
  CARGO_ARGS=(build --manifest-path rust/Cargo.toml -p photo-catalog --bins)
  if [ -n "${BUILD_JOBS}" ]; then
    CARGO_ARGS+=(--jobs "${BUILD_JOBS}")
  fi
  run_step "Rust catalog binaries" cargo "${CARGO_ARGS[@]}"
  if [ "${FORCE_CONFIGURE}" -eq 1 ] ||
     [ ! -f "${BUILD_DIR}/CMakeCache.txt" ] || [ ! -f "${BUILD_DIR}/build.ninja" ] ||
     ! grep -Fxq "PHOTO_EDITOR_RUST_BINARY_DIR:PATH=${RUST_BIN_DIR}" "${BUILD_DIR}/CMakeCache.txt"; then
    run_step "Desktop configure" \
      cmake --preset "${CONFIGURE_PRESET}" \
      -DPHOTO_EDITOR_RUST_BINARY_DIR="${RUST_BIN_DIR}"
  fi
  BUILD_ARGS=(--build --preset "${BUILD_PRESET}" --target photo_editor_app)
  if [ -n "${BUILD_JOBS}" ]; then
    BUILD_ARGS+=(--parallel "${BUILD_JOBS}")
  fi
  run_step "Desktop build" cmake "${BUILD_ARGS[@]}"
fi

[ -x "${APP_BINARY}" ] || fail "App not built: ${APP_BINARY}. Run without --no-build first."
if [ "${BUILD_ONLY}" -eq 1 ]; then
  note "App ready: ${APP_BINARY}"
  exit 0
fi

PHOTO_EDITOR_APP_DATA_DIR="${PHOTO_EDITOR_APP_DATA_DIR:-${HOME}/Library/Application Support/PhotoEditor}"
PHOTO_EDITOR_LOG_DIR="${PHOTO_EDITOR_LOG_DIR:-${PHOTO_EDITOR_APP_DATA_DIR}/logs}"
PHOTO_EDITOR_BACKUP_DIR="${PHOTO_EDITOR_BACKUP_DIR:-${PHOTO_EDITOR_APP_DATA_DIR}/backups}"
export PHOTO_EDITOR_APP_DATA_DIR PHOTO_EDITOR_LOG_DIR PHOTO_EDITOR_BACKUP_DIR

if [ -n "${PHOTO_EDITOR_DATABASE_URL:-}" ]; then
  CATALOG_HELPER="${RUST_BIN_DIR}/catalog_migrate"
else
  CATALOG_HELPER="${RUST_BIN_DIR}/catalog_local_db"
fi
[ -x "${CATALOG_HELPER}" ] || fail "Catalog helper not built: ${CATALOG_HELPER}. Run without --no-build first."
mkdir -p "${PHOTO_EDITOR_APP_DATA_DIR}" "${PHOTO_EDITOR_LOG_DIR}" "${PHOTO_EDITOR_BACKUP_DIR}"
note "Runtime data: ${PHOTO_EDITOR_APP_DATA_DIR}"
note "Logs: ${PHOTO_EDITOR_LOG_DIR}"
if [ -n "${PHOTO_EDITOR_DATABASE_URL:-}" ]; then
  run_step "External PostgreSQL catalog migrations" "${CATALOG_HELPER}"
else
  run_step "Managed local PostgreSQL catalog" "${CATALOG_HELPER}" ensure-running
fi

# Both configurations share the same catalog/outbox. Switching to Release must
# close this checkout's Debug instance too, before the new app acquires the lock.
APP_PATTERN="$(printf '%s' "${REPO_ROOT}/build/" | sed 's/[][(){}.^$*+?|\\]/\\&/g')"'(dev-macos-debug|release-macos)/apps/desktop/Looksy\.app/Contents/MacOS/Looksy'
EXISTING_APP_PIDS=""
if EXISTING_APP_PIDS="$(pgrep -u "$(id -u)" -f "^${APP_PATTERN}([[:space:]]|$)")"; then
  warn "Stopping existing Looksy instance before launch"
  while IFS= read -r pid; do
    [ -n "${pid}" ] || continue
    kill "${pid}" 2>/dev/null || true
    attempts=0
    while kill -0 "${pid}" 2>/dev/null; do
      if [ "${attempts}" -ge 100 ]; then
        fail "Looksy (PID ${pid}) has not exited. Close it before launching again."
      fi
      sleep 0.1
      attempts=$((attempts + 1))
    done
  done <<< "${EXISTING_APP_PIDS}"
else
  status=$?
  [ "${status}" -eq 1 ] || fail "Could not check whether Looksy is already running."
fi

note "App binary: ${APP_BINARY}"
note "Ready in $(duration_since "${started_at}")"
log_line "${MAGENTA}" "launch" "Looksy is taking over this terminal"
exec "${APP_BINARY}" "$@"
