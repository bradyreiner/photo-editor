#!/usr/bin/env bash

set -euo pipefail

archive_path="${PHOTO_EDITOR_SPARKLE_ARCHIVE:-}"
archive_url="${PHOTO_EDITOR_SPARKLE_URL:-}"
destination_root="${PHOTO_EDITOR_SPARKLE_DEST:-${RUNNER_TEMP:-/tmp}/photo-editor-sparkle}"
downloaded_archive=""

usage() {
  cat <<'EOF'
Prepare a Sparkle distribution directory for packaging.

Inputs via environment:
  PHOTO_EDITOR_SPARKLE_ARCHIVE  Local Sparkle archive path (.zip, .tar, .tar.gz, .tgz)
  PHOTO_EDITOR_SPARKLE_URL      Remote Sparkle archive URL
  PHOTO_EDITOR_SPARKLE_DEST     Extraction destination root

Outputs:
  Prints the normalized Sparkle distribution root on stdout.
  Writes distribution_dir=<path> to GITHUB_OUTPUT when available.
EOF
}

cleanup() {
  if [ -n "${downloaded_archive}" ] && [ -f "${downloaded_archive}" ]; then
    rm -f "${downloaded_archive}"
  fi
}
trap cleanup EXIT

if [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

if [ -z "${archive_path}" ] && [ -z "${archive_url}" ]; then
  echo "Either PHOTO_EDITOR_SPARKLE_ARCHIVE or PHOTO_EDITOR_SPARKLE_URL must be set." >&2
  exit 1
fi

if [ -n "${archive_url}" ]; then
  downloaded_archive="${destination_root}.download"
  mkdir -p "$(dirname "${downloaded_archive}")"
  curl --fail --location --silent --show-error "${archive_url}" --output "${downloaded_archive}"
  archive_path="${downloaded_archive}"
fi

if [ ! -f "${archive_path}" ]; then
  echo "Sparkle archive not found: ${archive_path}" >&2
  exit 1
fi

rm -rf "${destination_root}"
mkdir -p "${destination_root}"

case "${archive_path}" in
  *.zip)
    if command -v unzip >/dev/null 2>&1; then
      unzip -q "${archive_path}" -d "${destination_root}"
    else
      tar -xf "${archive_path}" -C "${destination_root}"
    fi
    ;;
  *.tar|*.tar.gz|*.tgz)
    tar -xf "${archive_path}" -C "${destination_root}"
    ;;
  *)
    echo "Unsupported Sparkle archive format: ${archive_path}" >&2
    exit 1
    ;;
esac

find_distribution_root() {
  local candidate
  while IFS= read -r candidate; do
    if [ -d "${candidate}/Sparkle.framework" ] && [ -x "${candidate}/bin/generate_appcast" ]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done < <(find "${destination_root}" -type d | sort)
  return 1
}

distribution_dir="$(find_distribution_root || true)"
if [ -z "${distribution_dir}" ]; then
  echo "Unable to locate a Sparkle distribution root containing Sparkle.framework and bin/generate_appcast under ${destination_root}." >&2
  exit 1
fi

printf '%s\n' "${distribution_dir}"
if [ -n "${GITHUB_OUTPUT:-}" ]; then
  printf 'distribution_dir=%s\n' "${distribution_dir}" >> "${GITHUB_OUTPUT}"
fi
