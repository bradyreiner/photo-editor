#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

version="$(tr -d '[:space:]' < "${REPO_ROOT}/VERSION")"
package_dir="${REPO_ROOT}/build/release-macos/package"
dmg_path=""
require_signature=0
allow_no_sparkle=0

usage() {
  cat <<'EOF'
Verify a packaged macOS Looksy DMG.

Usage:
  scripts/verify-macos-release.sh [options]

Options:
  --version X.Y.Z       Release version. Defaults to VERSION.
  --package-dir PATH    Directory containing the packaged DMG.
  --dmg PATH            DMG path to verify.
  --require-signature   Require Developer ID signature assessment to pass.
  --allow-no-sparkle    Allow a local package built without the Sparkle updater.
  --help                Show this help text.
EOF
}

log() {
  printf '[verify-macos] %s\n' "$*"
}

fail() {
  printf '[verify-macos] error: %s\n' "$*" >&2
  exit 1
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --version)
      [ "$#" -ge 2 ] || fail "--version requires a value"
      version="$2"
      shift 2
      ;;
    --package-dir)
      [ "$#" -ge 2 ] || fail "--package-dir requires a value"
      package_dir="$2"
      shift 2
      ;;
    --dmg)
      [ "$#" -ge 2 ] || fail "--dmg requires a value"
      dmg_path="$2"
      shift 2
      ;;
    --require-signature)
      require_signature=1
      shift
      ;;
    --allow-no-sparkle)
      allow_no_sparkle=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      fail "unknown option: $1"
      ;;
  esac
done

if [ -z "${dmg_path}" ]; then
  dmg_path="$(find "${package_dir}" -maxdepth 2 -type f -name "Looksy-${version}-Darwin-*.dmg" | head -n 1)"
fi

[ -f "${dmg_path}" ] || fail "DMG not found: ${dmg_path}"

checksum_file="$(dirname "${dmg_path}")/SHA256SUMS.txt"
if [ -f "${checksum_file}" ]; then
  log "Verifying SHA-256 checksums"
  (cd "$(dirname "${dmg_path}")" && shasum -a 256 -c "$(basename "${checksum_file}")")
fi

log "Verifying DMG structure"
hdiutil verify "${dmg_path}"

mount_point="$(mktemp -d /tmp/photo-editor-dmg-check.XXXXXX)"
detach() {
  hdiutil detach "${mount_point}" >/dev/null 2>&1 || true
  rmdir "${mount_point}" >/dev/null 2>&1 || true
}
trap detach EXIT

log "Mounting DMG read-only"
hdiutil attach -readonly -nobrowse -mountpoint "${mount_point}" "${dmg_path}" >/dev/null

app_path="${mount_point}/Looksy.app"
[ -d "${app_path}" ] || fail "Looksy.app not found inside DMG"

[ -x "${app_path}/Contents/MacOS/Looksy" ] || fail "missing app executable: Looksy"
while IFS= read -r helper; do
  [ -n "${helper}" ] || continue
  [ -x "${app_path}/Contents/MacOS/${helper}" ] || fail "missing executable helper: ${helper}"
done <"${REPO_ROOT}/cmake/CatalogHelpers.txt"

postgres_root="${app_path}/Contents/Resources/postgres"
[ -d "${postgres_root}/bin" ] || fail "bundled PostgreSQL runtime is missing"
for postgres_tool in initdb pg_ctl pg_dump pg_restore postgres psql; do
  [ -x "${postgres_root}/bin/${postgres_tool}" ] ||
    fail "bundled PostgreSQL tool is missing: ${postgres_tool}"
done
log "Managed PostgreSQL runtime is present"

log "Running isolated managed/external PostgreSQL catalog lifecycle"
PHOTO_EDITOR_CATALOG_BIN_DIR="${app_path}/Contents/MacOS" \
PHOTO_EDITOR_POSTGRES_RUNTIME_DIR="${postgres_root}" \
  "${REPO_ROOT}/scripts/test-postgres-catalog-runtime.sh"

[ -d "${app_path}/Contents/Frameworks" ] || fail "Qt frameworks were not bundled under Contents/Frameworks"
if [ "${allow_no_sparkle}" -eq 0 ]; then
  [ -d "${app_path}/Contents/Frameworks/Sparkle.framework" ] || fail "Sparkle.framework was not bundled"
  feed_url="$(plutil -extract SUFeedURL raw "${app_path}/Contents/Info.plist" 2>/dev/null || true)"
  public_key="$(plutil -extract SUPublicEDKey raw "${app_path}/Contents/Info.plist" 2>/dev/null || true)"
  [ -n "${feed_url}" ] || fail "Sparkle feed URL is missing from Info.plist"
  [ -n "${public_key}" ] || fail "Sparkle public EdDSA key is missing from Info.plist"
else
  log "Skipping Sparkle checks for an explicitly allowed local package"
fi
[ -f "${app_path}/Contents/Resources/licenses/LICENSE" ] || fail "application license is missing"
[ -f "${app_path}/Contents/Resources/licenses/THIRD_PARTY_NOTICES.md" ] || fail "third-party notices are missing"

log "Checking the complete Mach-O dependency closure"
"${REPO_ROOT}/scripts/verify-macos-bundle-dependencies.sh" "${app_path}"

if [ "${require_signature}" -eq 1 ]; then
  log "Verifying Developer ID signature"
  codesign --verify --deep --strict --verbose=2 "${app_path}"
  spctl --assess --type execute --verbose=4 "${app_path}"
  spctl --assess --type open --context context:primary-signature --verbose=4 "${dmg_path}"
fi

log "macOS release verification passed"
