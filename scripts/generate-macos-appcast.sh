#!/usr/bin/env bash

set -euo pipefail

distribution_dir="${PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR:-}"
package_dir="${PHOTO_EDITOR_MACOS_PACKAGE_DIR:-}"

usage() {
  cat <<'EOF'
Generate a Sparkle appcast from packaged macOS release artifacts.

Inputs via environment:
  PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR  Sparkle distribution root containing bin/generate_appcast
  PHOTO_EDITOR_MACOS_PACKAGE_DIR         Directory containing packaged archives (for example DMGs)
  PHOTO_EDITOR_ALLOW_UNSIGNED_APPCAST    Set to 1 to generate an appcast for unsigned local test artifacts
EOF
}

if [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

if [ -z "${distribution_dir}" ]; then
  echo "PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR must be set." >&2
  exit 1
fi

if [ -z "${package_dir}" ]; then
  echo "PHOTO_EDITOR_MACOS_PACKAGE_DIR must be set." >&2
  exit 1
fi

tool="${distribution_dir}/bin/generate_appcast"
if [ ! -x "${tool}" ]; then
  echo "Sparkle appcast tool not found: ${tool}" >&2
  exit 1
fi

if [ "${PHOTO_EDITOR_ALLOW_UNSIGNED_APPCAST:-0}" != "1" ]; then
  shopt -s nullglob
  dmg_files=("${package_dir}"/*.dmg)
  shopt -u nullglob
  if [ "${#dmg_files[@]}" -eq 0 ]; then
    echo "No DMG artifacts found in ${package_dir}." >&2
    exit 1
  fi
  for dmg_file in "${dmg_files[@]}"; do
    if ! codesign --verify "${dmg_file}" >/dev/null 2>&1; then
      echo "Refusing to generate a Sparkle appcast for unsigned artifact: ${dmg_file}" >&2
      echo "Run scripts/sign-notarize-macos.sh first, or set PHOTO_EDITOR_ALLOW_UNSIGNED_APPCAST=1 for local test artifacts." >&2
      exit 1
    fi
  done
fi

"${tool}" "${package_dir}"
