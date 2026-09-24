#!/usr/bin/env bash

set -euo pipefail

bundle=""
identity=""
hardened_runtime=0
timestamp=0

usage() {
  cat <<'EOF'
Sign a macOS app bundle deterministically from Mach-O leaves to its outer seal.

Usage:
  scripts/sign-macos-bundle.sh APP_BUNDLE IDENTITY [options]

Options:
  --hardened-runtime  Add the hardened-runtime code-signing option.
  --timestamp         Request a trusted timestamp from Apple.
  --help              Show this help text.

Use identity '-' for a local ad-hoc signature.
EOF
}

fail() {
  printf '[sign-macos-bundle] error: %s\n' "$*" >&2
  exit 1
}

[ "$#" -ge 2 ] || {
  usage >&2
  exit 2
}
bundle="$1"
identity="$2"
shift 2

while [ "$#" -gt 0 ]; do
  case "$1" in
    --hardened-runtime)
      hardened_runtime=1
      shift
      ;;
    --timestamp)
      timestamp=1
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

[ -d "${bundle}" ] || fail "app bundle was not found: ${bundle}"
[ -n "${identity}" ] || fail "a signing identity is required"
command -v codesign >/dev/null 2>&1 || fail "codesign is required"
command -v file >/dev/null 2>&1 || fail "file is required"

bundle_parent="$(cd "$(dirname "${bundle}")" && pwd -P)"
bundle="${bundle_parent}/$(basename "${bundle}")"

# The search receipt is also compiled into the app. Re-signing these leaves
# would silently invalidate it. Verify the prepared artifact before ANY writes,
# preserve it during outer signing, then check it again after the outer seal.
search_runtime="${bundle}/Contents/Resources/search-runtime"
verify_search_runtime=0
search_check_arguments=()
if [ -e "${search_runtime}" ] || [ -L "${search_runtime}" ] ||
   { [ -f "${bundle}/Contents/Info.plist" ] &&
     grep -q 'LooksySearchRuntimeReceiptSHA256' "${bundle}/Contents/Info.plist"; }; then
  command -v python3 >/dev/null 2>&1 || fail "python3 is required to verify the prepared search runtime"
  signer_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
  search_check_arguments=("${signer_directory}/build-search-runtime.py"
    --verify-app-signing "${bundle}" --identity "${identity}")
  if [ "${hardened_runtime}" -eq 1 ]; then search_check_arguments+=(--hardened-runtime); fi
  if [ "${timestamp}" -eq 1 ]; then search_check_arguments+=(--timestamp); fi
  python3 -I "${search_check_arguments[@]}" || fail "search runtime preflight failed; prepare it before building the app"
  verify_search_runtime=1
fi

codesign_arguments=(--force)
if [ "${hardened_runtime}" -eq 1 ]; then
  codesign_arguments+=(--options runtime)
fi
if [ "${timestamp}" -eq 1 ]; then
  codesign_arguments+=(--timestamp)
fi
codesign_arguments+=(--sign "${identity}")

is_macho() {
  file -b "$1" | grep -q 'Mach-O'
}

# Sign all executable leaves first. macdeployqt can leave rewritten plug-ins
# with stale signatures when its helper processes outlive their QProcess
# wrappers; re-signing every Mach-O leaf makes the result deterministic.
while IFS= read -r -d '' candidate; do
  case "${candidate}" in "${search_runtime}"/*) continue ;; esac
  if is_macho "${candidate}"; then
    codesign "${codesign_arguments[@]}" "${candidate}"
  fi
done < <(find "${bundle}" -type f -print0 | sort -z)

# Seal nested code containers deepest-first after their leaves are final.
while IFS= read -r -d '' nested_bundle; do
  case "${nested_bundle}" in
    "${bundle}") ;;
    "${search_runtime}"/*) ;;
    *) codesign "${codesign_arguments[@]}" "${nested_bundle}" ;;
  esac
done < <(
  find "${bundle}" -type d \
    \( -name '*.app' -o -name '*.framework' -o -name '*.xpc' -o -name '*.appex' \) \
    -print0 | sort -rz
)

codesign "${codesign_arguments[@]}" "${bundle}"
codesign --verify --deep --strict --verbose=2 "${bundle}"
if [ "${verify_search_runtime}" -eq 1 ]; then
  python3 -I "${search_check_arguments[@]}" || fail "outer signing changed the prepared search runtime"
fi
