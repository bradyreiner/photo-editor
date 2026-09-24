#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VERSION_FILE="${REPO_ROOT}/VERSION"

release_version=""
version_changed=0
run_tests=1
require_clean=1
package_dir=""
create_tag=0
push_tag=0
postgres_runtime_dir="${PHOTO_EDITOR_POSTGRES_RUNTIME_DIR:-}"
legal_publisher="${LOOKSY_LEGAL_PUBLISHER:-${FRAMEHOUSE_LEGAL_PUBLISHER:-}}"

usage() {
  cat <<'EOF'
Build a production release package for Looksy.

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
  --skip-tests                 Skip Rust and native test gates.
  --allow-dirty                Permit building from an uncommitted worktree.
  --create-tag                 Create an annotated git tag vX.Y.Z after a successful build.
  --push-tag                   Push the created tag to origin after a successful build.
  --help                       Show this help text.

Environment:
  LOOKSY_LEGAL_PUBLISHER                 Approved legal publisher for package metadata.
  PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR   Sparkle distribution root for appcast generation.
  PHOTO_EDITOR_POSTGRES_RUNTIME_DIR       PostgreSQL runtime root to bundle.
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

postgres_runtime_has_required_tools() {
  local candidate="$1"
  [ -n "${candidate}" ] &&
    [ -x "${candidate}/bin/initdb" ] &&
    [ -x "${candidate}/bin/pg_ctl" ] &&
    [ -x "${candidate}/bin/pg_dump" ] &&
    [ -x "${candidate}/bin/pg_restore" ] &&
    [ -x "${candidate}/bin/postgres" ] &&
    [ -x "${candidate}/bin/psql" ]
}

postgres_runtime_has_required_support_files() {
  local candidate="$1"
  [ -d "${candidate}/share" ] &&
    find "${candidate}/share" -type f -name postgres.bki -print -quit | grep -q .
}

postgres_runtime_has_forbidden_macos_paths() {
  local candidate="$1"
  local binary dependency
  while IFS= read -r -d '' binary; do
    file -b "${binary}" | grep -q 'Mach-O' || continue
    while IFS= read -r dependency; do
      case "${dependency}" in
        /opt/homebrew/*|/usr/local/opt/*)
          printf '%s -> %s\n' "${binary}" "${dependency}" >&2
          return 0
          ;;
      esac
    done < <(otool -L "${binary}" 2>/dev/null | sed '1d' | awk '{print $1}')
  done < <(find "${candidate}" -type f -print0)
  return 1
}

detect_postgres_runtime_dir() {
  local pg_ctl_path
  pg_ctl_path="$(command -v pg_ctl 2>/dev/null || true)"
  if [ -n "${pg_ctl_path}" ]; then
    local candidate
    candidate="$(cd "$(dirname "${pg_ctl_path}")/.." && pwd)"
    if postgres_runtime_has_required_tools "${candidate}"; then
      printf '%s\n' "${candidate}"
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
      shift 2
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

    ;;
  *)
    fail "this release script currently supports macOS packaging from this machine. Use scripts/package-windows-msix.ps1 on Windows for MSIX releases."
    ;;
esac

if [ -z "${postgres_runtime_dir}" ]; then
  postgres_runtime_dir="$(detect_postgres_runtime_dir || true)"
fi
if [ -n "${postgres_runtime_dir}" ] && [ -d "${postgres_runtime_dir}" ]; then
  postgres_runtime_dir="$(cd "${postgres_runtime_dir}" && pwd -P)"
fi
postgres_runtime_has_required_tools "${postgres_runtime_dir}" ||
  fail "A PostgreSQL runtime containing initdb, pg_ctl, pg_dump, pg_restore, postgres, and psql is required. Set PHOTO_EDITOR_POSTGRES_RUNTIME_DIR or pass --postgres-runtime-dir."
postgres_runtime_has_required_support_files "${postgres_runtime_dir}" ||
  fail "The PostgreSQL runtime is missing initdb support data (share/**/postgres.bki)."
if [ "${host_os}" = "Darwin" ] && postgres_runtime_has_forbidden_macos_paths "${postgres_runtime_dir}"; then
  log "Relocating PostgreSQL and its non-system dylibs for packaging"
  postgres_runtime_dir="$(
    "${SCRIPT_DIR}/prepare-macos-postgres-runtime.sh" \
      "${postgres_runtime_dir}" \
      "${build_dir}/postgres-runtime"
  )"
  if postgres_runtime_has_forbidden_macos_paths "${postgres_runtime_dir}"; then
    fail "The relocated PostgreSQL runtime still contains package-manager load paths."
  fi
fi
if [ -z "${legal_publisher}" ] || [ "${legal_publisher}" = "Publisher identity pending release approval" ]; then
  fail "Set LOOKSY_LEGAL_PUBLISHER to the approved legal publisher before production packaging."
fi
if [ -z "${PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR:-}" ] ||
   [ -z "${PHOTO_EDITOR_MACOS_APPCAST_URL:-}" ] ||
   [ -z "${PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY:-}" ]; then
  fail "Production macOS packaging requires PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR, PHOTO_EDITOR_MACOS_APPCAST_URL, and PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY."
fi

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
cargo build --manifest-path "${REPO_ROOT}/rust/Cargo.toml" -p photo-catalog --bin catalog_helper --release

configure_args=(
  "-DLOOKSY_CUSTOMER_API_URL=${LOOKSY_CUSTOMER_API_URL:-}"
  "-DLOOKSY_ENTITLEMENT_PUBLIC_KEY=${LOOKSY_ENTITLEMENT_PUBLIC_KEY:-}"
  "-DLOOKSY_ENTITLEMENT_KEY_ID=${LOOKSY_ENTITLEMENT_KEY_ID:-looksy-1}"
  "-DLOOKSY_RELEASE_CHANNEL=${LOOKSY_RELEASE_CHANNEL:-stable}"
  --preset "${configure_preset}"
  "-DPHOTO_EDITOR_RUST_BINARY_DIR=${REPO_ROOT}/rust/target/release"
  "-DPHOTO_EDITOR_STRICT_PACKAGING=ON"
  "-DPHOTO_EDITOR_POSTGRES_RUNTIME_DIR=${postgres_runtime_dir}"
  "-DFRAMEHOUSE_LEGAL_PUBLISHER=${legal_publisher}"
  "-DPHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER=ON"
  "-DPHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR=${PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR}"
  "-DPHOTO_EDITOR_MACOS_APPCAST_URL=${PHOTO_EDITOR_MACOS_APPCAST_URL}"
  "-DPHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY=${PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY}"
)

log "Sparkle updater integration is enabled for this build"

log "Configuring ${configure_preset}"
cmake "${configure_args[@]}"

log "Building ${build_preset}"
cmake --build --preset "${build_preset}"

if [ "${run_tests}" -eq 1 ]; then
  log "Running mask release gate"
  "${REPO_ROOT}/scripts/run-mask-release-gate.sh" --build-dir "${build_dir}"
fi

log "Packaging release artifacts into ${package_dir}"
mkdir -p "${package_dir}"
find "${package_dir}" -maxdepth 1 -type f -name "Looksy-${release_version}-*" -delete
rm -f "${package_dir}/SHA256SUMS.txt"
cpack --config "${build_dir}/CPackConfig.cmake" -B "${package_dir}"

if [ -n "${PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR:-}" ] &&
   [ -n "${PHOTO_EDITOR_MACOS_APPCAST_URL:-}" ] &&
   [ -n "${PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY:-}" ]; then
  log "Deferring Sparkle appcast generation until after signing/notarization"
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
