#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VERSION_FILE="${REPO_ROOT}/VERSION"

release_version=""
version_changed=0
run_tests=1
require_clean=1
use_postgres_runtime=1
postgres_runtime_dir="${PHOTO_EDITOR_POSTGRES_RUNTIME_DIR:-}"
package_dir=""
create_tag=0
push_tag=0

usage() {
  cat <<'EOF'
Build a production release package for PhotoEditor.

Usage:
  scripts/build-release.sh [version] [options]

Examples:
  scripts/build-release.sh 0.1.0
  scripts/build-release.sh 0.1.1 --allow-dirty --skip-tests
  scripts/build-release.sh 0.2.0 --create-tag

Options:
  --version X.Y.Z              Release version. Same as the positional version.
  --package-dir PATH           Output directory for packaged artifacts.
  --postgres-runtime-dir PATH  Bundle this PostgreSQL runtime root.
  --no-postgres-runtime        Do not bundle a PostgreSQL runtime.
  --skip-tests                 Skip Rust and native test gates.
  --allow-dirty                Permit building from an uncommitted worktree.
  --create-tag                 Create an annotated git tag vX.Y.Z after a successful build.
  --push-tag                   Push the created tag to origin after a successful build.
  --help                       Show this help text.

Environment:
  PHOTO_EDITOR_POSTGRES_RUNTIME_DIR       PostgreSQL runtime root to bundle.
  PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR   Sparkle distribution root for appcast generation.
  PHOTO_EDITOR_MACOS_APPCAST_URL          Enables Sparkle appcast generation when set with Sparkle.
  PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY   Enables Sparkle appcast generation when set with Sparkle.
EOF
}

log() {
  printf '[release] %s\n' "$*"
}

fail() {
  printf '[release] error: %s\n' "$*" >&2
  exit 1
}

detect_postgres_runtime_dir() {
  local formula
  local candidate
  local pg_ctl_path
  local initdb_path
  local resolved_pg_ctl_path
  local runtime_root

  if command -v brew >/dev/null 2>&1; then
    for formula in postgresql@16 postgresql@15 postgresql@14 postgresql; do
      candidate="$(brew --prefix "${formula}" 2>/dev/null || true)"
      if [ -n "${candidate}" ] && [ -d "${candidate}/bin" ]; then
        printf '%s\n' "${candidate}"
        return 0
      fi
    done
  fi

  pg_ctl_path="$(command -v pg_ctl 2>/dev/null || true)"
  initdb_path="$(command -v initdb 2>/dev/null || true)"
  if [ -n "${pg_ctl_path}" ] && [ -n "${initdb_path}" ]; then
    resolved_pg_ctl_path="$(realpath "${pg_ctl_path}")"
    runtime_root="$(cd "$(dirname "${resolved_pg_ctl_path}")/.." && pwd)"
    if [ -f "${runtime_root}/bin/pg_ctl" ] && [ -f "${runtime_root}/bin/initdb" ]; then
      printf '%s\n' "${runtime_root}"
      return 0
    fi
  fi

  return 1
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --version)
      [ "$#" -ge 2 ] || fail "--version requires a value"
      release_version="$2"
      shift 2
      ;;
    --package-dir)
      [ "$#" -ge 2 ] || fail "--package-dir requires a value"
      package_dir="$2"
      shift 2
      ;;
    --postgres-runtime-dir)
      [ "$#" -ge 2 ] || fail "--postgres-runtime-dir requires a value"
      postgres_runtime_dir="$2"
      use_postgres_runtime=1
      shift 2
      ;;
    --no-postgres-runtime)
      use_postgres_runtime=0
      postgres_runtime_dir=""
      shift
      ;;
    --skip-tests)
      run_tests=0
      shift
      ;;
    --allow-dirty)
      require_clean=0
      shift
      ;;
    --create-tag)
      create_tag=1
      shift
      ;;
    --push-tag)
      create_tag=1
      push_tag=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --*)
      fail "unknown option: $1"
      ;;
    *)
      if [ -n "${release_version}" ]; then
        fail "release version was provided more than once"
      fi
      release_version="$1"
      shift
      ;;
  esac
done

cd "${REPO_ROOT}"

if [ -z "${release_version}" ]; then
  release_version="$(tr -d '[:space:]' < "${VERSION_FILE}")"
fi

if ! printf '%s' "${release_version}" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
  fail "version must be in X.Y.Z form, got: ${release_version}"
fi

if [ "${require_clean}" -eq 1 ] && ! git diff --quiet --ignore-submodules --; then
  fail "worktree has uncommitted changes. Commit them first or rerun with --allow-dirty for a local candidate build."
fi

if [ "${require_clean}" -eq 1 ] && [ -n "$(git status --short --untracked-files=normal)" ]; then
  fail "worktree has untracked files. Commit them first or rerun with --allow-dirty for a local candidate build."
fi

current_version="$(tr -d '[:space:]' < "${VERSION_FILE}")"
if [ "${current_version}" != "${release_version}" ]; then
  log "Updating VERSION from ${current_version} to ${release_version}"
  printf '%s\n' "${release_version}" > "${VERSION_FILE}"
  version_changed=1
fi

if [ "${create_tag}" -eq 1 ] && [ "${version_changed}" -eq 1 ]; then
  fail "VERSION was updated but is not committed. Commit the version bump, then rerun with --create-tag."
fi

for tool in cargo cmake cpack git; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    fail "${tool} is required"
  fi
done

host_os="$(uname -s)"
case "${host_os}" in
  Darwin)
    configure_preset="release-macos"
    build_preset="build-release-macos"
    build_dir="${REPO_ROOT}/build/${configure_preset}"
    package_dir="${package_dir:-${build_dir}/package}"

    if [ -z "${CMAKE_PREFIX_PATH:-}" ] && command -v brew >/dev/null 2>&1; then
      qt_prefix="$(brew --prefix qt 2>/dev/null || true)"
      if [ -n "${qt_prefix}" ]; then
        export CMAKE_PREFIX_PATH="${qt_prefix}"
        export PATH="${qt_prefix}/bin:${PATH}"
      fi
    fi

    if [ "${use_postgres_runtime}" -eq 1 ] && [ -z "${postgres_runtime_dir}" ]; then
      postgres_runtime_dir="$(detect_postgres_runtime_dir || true)"
    fi
    ;;
  *)
    fail "this release script currently supports macOS packaging from this machine. Use scripts/package-windows-msix.ps1 on Windows for MSIX releases."
    ;;
esac

if [ "${run_tests}" -eq 1 ]; then
  log "Running Rust tests"
  cargo test --manifest-path "${REPO_ROOT}/rust/Cargo.toml" --workspace

  log "Running headless native tests"
  cmake --preset ci-headless
  cmake --build --preset build-ci-headless
  ctest --preset test-ci-headless --output-on-failure
else
  log "Skipping tests by request"
fi

log "Building Rust helper binaries for release"
cargo build --manifest-path "${REPO_ROOT}/rust/Cargo.toml" -p photo-catalog --bins --release

configure_args=(
  --preset "${configure_preset}"
  "-DPHOTO_EDITOR_RUST_BINARY_DIR=${REPO_ROOT}/rust/target/release"
  "-DPHOTO_EDITOR_STRICT_PACKAGING=ON"
)

if [ "${use_postgres_runtime}" -eq 1 ]; then
  if [ -n "${postgres_runtime_dir}" ] && [ -d "${postgres_runtime_dir}/bin" ]; then
    configure_args+=("-DPHOTO_EDITOR_POSTGRES_RUNTIME_DIR=${postgres_runtime_dir}")
    log "Bundling PostgreSQL runtime from ${postgres_runtime_dir}"
  else
    fail "PostgreSQL runtime was requested but not found. Install postgresql@16, set PHOTO_EDITOR_POSTGRES_RUNTIME_DIR, pass --postgres-runtime-dir, or pass --no-postgres-runtime."
  fi
else
  log "Building without bundled PostgreSQL runtime"
fi

if [ -n "${PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR:-}" ] &&
   [ -n "${PHOTO_EDITOR_MACOS_APPCAST_URL:-}" ] &&
   [ -n "${PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY:-}" ]; then
  configure_args+=(
    "-DPHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER=ON"
    "-DPHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR=${PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR}"
    "-DPHOTO_EDITOR_MACOS_APPCAST_URL=${PHOTO_EDITOR_MACOS_APPCAST_URL}"
    "-DPHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY=${PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY}"
  )
  log "Sparkle updater integration is enabled for this build"
fi

log "Configuring ${configure_preset}"
cmake "${configure_args[@]}"

log "Building ${build_preset}"
cmake --build --preset "${build_preset}"

log "Packaging release artifacts into ${package_dir}"
mkdir -p "${package_dir}"
find "${package_dir}" -maxdepth 1 -type f -name "PhotoEditor-${release_version}-*" -delete
rm -f "${package_dir}/SHA256SUMS.txt"
cpack --config "${build_dir}/CPackConfig.cmake" -B "${package_dir}"

if [ -n "${PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR:-}" ] &&
   [ -n "${PHOTO_EDITOR_MACOS_APPCAST_URL:-}" ] &&
   [ -n "${PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY:-}" ]; then
  log "Generating Sparkle appcast"
  PHOTO_EDITOR_MACOS_PACKAGE_DIR="${package_dir}" \
  "${SCRIPT_DIR}/generate-macos-appcast.sh"
fi

checksum_file="${package_dir}/SHA256SUMS.txt"
log "Writing checksums to ${checksum_file}"
(
  cd "${package_dir}"
  while IFS= read -r -d '' artifact; do
    shasum -a 256 "${artifact#./}"
  done < <(find . -maxdepth 1 -type f ! -name 'SHA256SUMS.txt' -print0 | sort -z)
) > "${checksum_file}"

release_tag="v${release_version}"
if [ "${create_tag}" -eq 1 ]; then
  if git rev-parse "${release_tag}" >/dev/null 2>&1; then
    fail "tag already exists: ${release_tag}"
  fi
  log "Creating annotated tag ${release_tag}"
  git tag -a "${release_tag}" -m "Release ${release_tag}"
  if [ "${push_tag}" -eq 1 ]; then
    log "Pushing tag ${release_tag} to origin"
    git push origin "${release_tag}"
  fi
fi

cat <<EOF

Release build complete.

Version:   ${release_version}
Artifacts: ${package_dir}
Checksums: ${checksum_file}

Before sending this release to users:
  1. Sign and notarize the macOS artifact.
  2. Test install/launch/import/edit/export/relaunch on a clean machine.
  3. Publish the artifact with SHA256SUMS.txt and release notes.
EOF
