#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

version="$(tr -d '[:space:]' < "${REPO_ROOT}/VERSION")"
package_dir="${REPO_ROOT}/build/release-macos/package"
dmg_path=""
require_signature=0

usage() {
  cat <<'EOF'
Verify a packaged macOS Virel Studio DMG.

Usage:
  scripts/verify-macos-release.sh [options]

Options:
  --version X.Y.Z       Release version. Defaults to VERSION.
  --package-dir PATH    Directory containing the packaged DMG.
  --dmg PATH            DMG path to verify.
  --require-signature   Require Developer ID signature assessment to pass.
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
  dmg_path="$(find "${package_dir}" -maxdepth 2 -type f -name "VirelStudio-${version}-Darwin-*.dmg" | head -n 1)"
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

app_path="${mount_point}/VirelStudio.app"
[ -d "${app_path}" ] || fail "VirelStudio.app not found inside DMG"

for helper in \
  VirelStudio \
  catalog_carousel_documents \
  catalog_export_jobs \
  catalog_local_db \
  catalog_migrate \
  catalog_snapshot \
  catalog_state \
  catalog_timeline_documents \
  import_catalog \
  save_edit_recipe; do
  [ -x "${app_path}/Contents/MacOS/${helper}" ] || fail "missing executable helper: ${helper}"
done

for postgres_tool in initdb pg_ctl pg_dump pg_restore; do
  [ -x "${app_path}/Contents/Resources/postgres/bin/${postgres_tool}" ] || fail "missing bundled PostgreSQL tool: ${postgres_tool}"
done

[ -d "${app_path}/Contents/Frameworks" ] || fail "Qt frameworks were not bundled under Contents/Frameworks"
[ -f "${app_path}/Contents/Resources/licenses/LICENSE" ] || fail "application license is missing"
[ -f "${app_path}/Contents/Resources/licenses/THIRD_PARTY_NOTICES.md" ] || fail "third-party notices are missing"

log "Checking Mach-O dynamic library paths"
while IFS= read -r -d '' candidate; do
  if file -b "${candidate}" | grep -q 'Mach-O'; then
    install_id="$(otool -D "${candidate}" 2>/dev/null | sed -n '2p' || true)"
    bad_paths="$(
      otool -L "${candidate}" | sed '1d' | awk '{print $1}' |
        while IFS= read -r dependency; do
          if [ "${dependency}" = "${install_id}" ]; then
            continue
          fi
          case "${dependency}" in
            /opt/homebrew/*|/usr/local/opt/*)
              printf '%s\n' "${dependency}"
              ;;
          esac
        done
    )"
    if [ -n "${bad_paths}" ]; then
      printf '%s\n' "${bad_paths}" >&2
      fail "${candidate#${app_path}/} still links to local package-manager paths"
    fi
  fi
done < <(find "${app_path}" -type f -print0)

if [ "${require_signature}" -eq 1 ]; then
  log "Verifying Developer ID signature"
  codesign --verify --deep --strict --verbose=2 "${app_path}"
  spctl --assess --type execute --verbose=4 "${app_path}"
  spctl --assess --type open --context context:primary-signature --verbose=4 "${dmg_path}"
fi

log "macOS release verification passed"
