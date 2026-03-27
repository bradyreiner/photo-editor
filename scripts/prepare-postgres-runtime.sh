#!/usr/bin/env bash

set -euo pipefail

archive_path="${PHOTO_EDITOR_POSTGRES_RUNTIME_ARCHIVE:-}"
archive_url="${PHOTO_EDITOR_POSTGRES_RUNTIME_URL:-}"
destination_root="${PHOTO_EDITOR_POSTGRES_RUNTIME_DEST:-${RUNNER_TEMP:-/tmp}/photo-editor-postgres-runtime}"
downloaded_archive=""

usage() {
  cat <<'EOF'
Prepare a PostgreSQL runtime directory for packaging.

Inputs via environment:
  PHOTO_EDITOR_POSTGRES_RUNTIME_ARCHIVE  Local archive path (.zip, .tar, .tar.gz, .tgz)
  PHOTO_EDITOR_POSTGRES_RUNTIME_URL      Remote archive URL to download
  PHOTO_EDITOR_POSTGRES_RUNTIME_DEST     Extraction destination root

Outputs:
  Prints the normalized runtime root on stdout.
  Writes runtime_dir=<path> to GITHUB_OUTPUT when available.
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
  echo "Either PHOTO_EDITOR_POSTGRES_RUNTIME_ARCHIVE or PHOTO_EDITOR_POSTGRES_RUNTIME_URL must be set." >&2
  exit 1
fi

if [ -n "${archive_url}" ]; then
  downloaded_archive="${destination_root}.download"
  mkdir -p "$(dirname "${downloaded_archive}")"
  curl --fail --location --silent --show-error "${archive_url}" --output "${downloaded_archive}"
  archive_path="${downloaded_archive}"
fi

if [ ! -f "${archive_path}" ]; then
  echo "PostgreSQL runtime archive not found: ${archive_path}" >&2
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
    echo "Unsupported PostgreSQL runtime archive format: ${archive_path}" >&2
    exit 1
    ;;
esac

find_runtime_root() {
  local candidate
  while IFS= read -r candidate; do
    if [ -f "${candidate}/bin/pg_ctl" ] || [ -f "${candidate}/bin/pg_ctl.exe" ]; then
      if [ -f "${candidate}/bin/initdb" ] || [ -f "${candidate}/bin/initdb.exe" ]; then
        printf '%s\n' "${candidate}"
        return 0
      fi
    fi
  done < <(find "${destination_root}" -type d | sort)
  return 1
}

runtime_dir="$(find_runtime_root || true)"
if [ -z "${runtime_dir}" ]; then
  echo "Unable to locate a PostgreSQL runtime root containing bin/pg_ctl and bin/initdb under ${destination_root}." >&2
  exit 1
fi

printf '%s\n' "${runtime_dir}"
if [ -n "${GITHUB_OUTPUT:-}" ]; then
  printf 'runtime_dir=%s\n' "${runtime_dir}" >> "${GITHUB_OUTPUT}"
fi
