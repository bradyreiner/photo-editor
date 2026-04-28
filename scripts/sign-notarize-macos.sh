#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

version="$(tr -d '[:space:]' < "${REPO_ROOT}/VERSION")"
package_dir="${REPO_ROOT}/build/release-macos/package"
source_app=""
identity="${PHOTO_EDITOR_MACOS_CODESIGN_IDENTITY:-}"
notary_profile="${PHOTO_EDITOR_NOTARY_PROFILE:-}"
skip_notarize=0
skip_app_notarize=0

usage() {
  cat <<'EOF'
Sign, notarize, staple, and package a macOS PhotoEditor release.

Usage:
  scripts/sign-notarize-macos.sh --identity "Developer ID Application: Name (TEAMID)" --notary-profile PROFILE [options]

Options:
  --version X.Y.Z          Release version. Defaults to VERSION.
  --package-dir PATH       Output directory. Defaults to build/release-macos/package.
  --app PATH               Staged PhotoEditor.app to sign. Defaults to CPack staging path.
  --identity NAME          Developer ID Application signing identity.
  --notary-profile NAME    notarytool keychain profile created with xcrun notarytool store-credentials.
  --skip-notarize          Sign and create a signed DMG without notarization.
  --skip-app-notarize      Notarize only the final DMG, not the app bundle zip first.
  --help                   Show this help text.

Environment:
  PHOTO_EDITOR_MACOS_CODESIGN_IDENTITY
  PHOTO_EDITOR_NOTARY_PROFILE
EOF
}

log() {
  printf '[sign-macos] %s\n' "$*"
}

fail() {
  printf '[sign-macos] error: %s\n' "$*" >&2
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
    --app)
      [ "$#" -ge 2 ] || fail "--app requires a value"
      source_app="$2"
      shift 2
      ;;
    --identity)
      [ "$#" -ge 2 ] || fail "--identity requires a value"
      identity="$2"
      shift 2
      ;;
    --notary-profile)
      [ "$#" -ge 2 ] || fail "--notary-profile requires a value"
      notary_profile="$2"
      shift 2
      ;;
    --skip-notarize)
      skip_notarize=1
      shift
      ;;
    --skip-app-notarize)
      skip_app_notarize=1
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

[ -n "${identity}" ] || fail "missing --identity or PHOTO_EDITOR_MACOS_CODESIGN_IDENTITY"
if [ "${skip_notarize}" -eq 0 ]; then
  [ -n "${notary_profile}" ] || fail "missing --notary-profile or PHOTO_EDITOR_NOTARY_PROFILE"
fi

if [ -z "${source_app}" ]; then
  source_app="${package_dir}/_CPack_Packages/Darwin/DragNDrop/PhotoEditor-${version}-Darwin/PhotoEditor.app"
fi

[ -d "${source_app}" ] || fail "staged app not found: ${source_app}"

if ! security find-identity -v -p codesigning | grep -F "${identity}" >/dev/null; then
  fail "signing identity was not found in the keychain: ${identity}"
fi

work_root="${package_dir}/signed-macos-${version}"
signed_root="${work_root}/dmg-root"
signed_app="${signed_root}/PhotoEditor.app"
app_zip="${work_root}/PhotoEditor-${version}-Darwin.app.zip"
signed_dmg="${work_root}/PhotoEditor-${version}-Darwin.dmg"

rm -rf "${work_root}"
mkdir -p "${signed_root}"
ditto "${source_app}" "${signed_app}"
ln -s /Applications "${signed_root}/Applications"

is_macho() {
  file -b "$1" | grep -q 'Mach-O'
}

log "Signing nested bundles"
while IFS= read -r -d '' bundle; do
  case "${bundle}" in
    "${signed_app}") ;;
    *)
      codesign --force --options runtime --timestamp --sign "${identity}" "${bundle}"
      ;;
  esac
done < <(find "${signed_app}" -type d \( -name '*.app' -o -name '*.framework' -o -name '*.xpc' -o -name '*.appex' \) -print0 | sort -rz)

log "Signing Mach-O files outside nested bundles"
while IFS= read -r -d '' candidate; do
  case "${candidate}" in
    *".framework/"*) continue ;;
  esac
  if is_macho "${candidate}"; then
    codesign --force --options runtime --timestamp --sign "${identity}" "${candidate}"
  fi
done < <(find "${signed_app}" -type f -print0 | sort -z)

log "Signing app bundle"
codesign --force --options runtime --timestamp --sign "${identity}" "${signed_app}"
codesign --verify --deep --strict --verbose=2 "${signed_app}"

if [ "${skip_notarize}" -eq 0 ] && [ "${skip_app_notarize}" -eq 0 ]; then
  log "Notarizing app bundle archive"
  ditto -c -k --keepParent "${signed_app}" "${app_zip}"
  xcrun notarytool submit "${app_zip}" --keychain-profile "${notary_profile}" --wait
  xcrun stapler staple "${signed_app}"
  xcrun stapler validate "${signed_app}"
fi

log "Creating signed DMG"
hdiutil create -ov -srcfolder "${signed_root}" -volname "PhotoEditor ${version}" -fs HFS+ -format UDZO "${signed_dmg}"

log "Signing DMG"
codesign --force --timestamp --sign "${identity}" "${signed_dmg}"
hdiutil verify "${signed_dmg}"

if [ "${skip_notarize}" -eq 0 ]; then
  log "Notarizing DMG"
  xcrun notarytool submit "${signed_dmg}" --keychain-profile "${notary_profile}" --wait
  xcrun stapler staple "${signed_dmg}"
  xcrun stapler validate "${signed_dmg}"
fi

checksum_file="${work_root}/SHA256SUMS.txt"
(
  cd "${work_root}"
  shasum -a 256 "$(basename "${signed_dmg}")"
) > "${checksum_file}"

cat <<EOF

Signed macOS release complete.

Artifact:  ${signed_dmg}
Checksums: ${checksum_file}
EOF
