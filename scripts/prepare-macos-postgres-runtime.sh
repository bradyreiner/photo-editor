#!/usr/bin/env bash

set -euo pipefail

source_runtime="${PHOTO_EDITOR_POSTGRES_RUNTIME_SOURCE:-}"
destination_root="${PHOTO_EDITOR_POSTGRES_RUNTIME_DEST:-}"

usage() {
  cat <<'EOF'
Create a relocatable macOS PostgreSQL runtime for Looksy packaging.

Usage:
  scripts/prepare-macos-postgres-runtime.sh SOURCE DESTINATION

Environment alternatives:
  PHOTO_EDITOR_POSTGRES_RUNTIME_SOURCE  PostgreSQL installation root
  PHOTO_EDITOR_POSTGRES_RUNTIME_DEST    Relocated runtime destination

The source may be a Homebrew installation. The output contains only the six
executables used by the app and release proofs, PostgreSQL's runtime libraries
and support data, plus recursively vendored non-system dylibs. Development
archives are excluded and Mach-O load commands are rewritten relative to the
relocated runtime.
EOF
}

fail() {
  printf 'prepare macOS PostgreSQL runtime: %s\n' "$*" >&2
  exit 1
}

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  usage
  exit 0
fi
if [ "$#" -gt 0 ]; then
  source_runtime="$1"
fi
if [ "$#" -gt 1 ]; then
  destination_root="$2"
fi
if [ "$#" -gt 2 ]; then
  fail "expected SOURCE and DESTINATION"
fi

[ "$(uname -s)" = "Darwin" ] || fail "this relocation step requires macOS"
for tool in codesign file install_name_tool otool python3; do
  command -v "${tool}" >/dev/null 2>&1 || fail "missing required tool: ${tool}"
done
[ -n "${source_runtime}" ] || fail "a PostgreSQL source runtime is required"
[ -n "${destination_root}" ] || fail "a destination is required"
[ -d "${source_runtime}/bin" ] || fail "source runtime has no bin directory"
[ -d "${source_runtime}/lib" ] || fail "source runtime has no lib directory"
[ -d "${source_runtime}/share" ] || fail "source runtime has no share directory"

source_runtime="$(cd "${source_runtime}" && pwd -P)"
destination_parent="$(dirname "${destination_root}")"
mkdir -p "${destination_parent}"
destination_parent="$(cd "${destination_parent}" && pwd -P)"
destination_root="${destination_parent}/$(basename "${destination_root}")"
case "${destination_root}" in
  /|/Users|/Applications|/opt|/usr|/private|"${HOME}"|"${source_runtime}")
    fail "refusing unsafe destination: ${destination_root}"
    ;;
esac

staging_root="${destination_root}.partial.$$"
cleanup() {
  case "${staging_root}" in
    "${destination_root}.partial."*) rm -rf -- "${staging_root}" ;;
  esac
}
trap cleanup EXIT
rm -rf -- "${staging_root}"
mkdir -p "${staging_root}/bin"
for tool in postgres initdb pg_ctl pg_dump pg_restore psql; do
  [ -x "${source_runtime}/bin/${tool}" ] || fail "source runtime is missing ${tool}"
  cp -p "${source_runtime}/bin/${tool}" "${staging_root}/bin/${tool}"
done
# Preserve internal relative symlinks. Dereferencing the Homebrew tree turns
# aliases such as postmaster -> postgres and libpq.dylib -> libpq.5.14.dylib
# into independent copies, hiding real package waste from both signing and the
# composition report.
cp -RPp "${source_runtime}/lib" "${staging_root}/lib"
cp -RPp "${source_runtime}/share" "${staging_root}/share"

# Static archives and PGXS are build-time inputs. The nested client dylibs are
# development aliases/copies; packaged tools resolve the relocated libraries
# from the runtime's top-level lib directory. Keep server modules (*.so),
# including encoding converters and PL/pgSQL, to preserve PostgreSQL behavior.
find "${staging_root}/lib" -type f -name '*.a' -delete
find "${staging_root}/lib" -type d -name pgxs -prune -exec rm -rf -- {} +
find "${staging_root}/lib" -type d -name pkgconfig -prune -exec rm -rf -- {} +
find "${staging_root}/lib" -mindepth 2 \( -type f -o -type l \) \
  -name '*.dylib' -delete
find "${staging_root}/share" -type d \( -name doc -o -name man \) \
  -prune -exec rm -rf -- {} +

is_macho() {
  file -b "$1" 2>/dev/null | grep -q 'Mach-O'
}

run_quietly() {
  local output
  if ! output="$("$@" 2>&1)"; then
    printf '%s\n' "${output}" >&2
    return 1
  fi
}

is_vendored_dependency() {
  case "$1" in
    /opt/homebrew/*|/usr/local/*|/opt/local/*) return 0 ;;
    *) return 1 ;;
  esac
}

while :; do
  copied=0
  while IFS= read -r -d '' candidate; do
    is_macho "${candidate}" || continue
    while IFS= read -r dependency; do
      dependency_source=""
      if is_vendored_dependency "${dependency}"; then
        dependency_source="${dependency}"
      elif [[ "${dependency}" = @loader_path/* ||
              "${dependency}" = @rpath/* ]]; then
        dependency_name="$(basename "${dependency}")"
        if [ ! -f "${staging_root}/lib/${dependency_name}" ]; then
          dependency_source="$(
            find -L /opt/homebrew/opt /usr/local/opt /opt/local/lib \
              -type f -name "${dependency_name}" -print -quit 2>/dev/null || true
          )"
        fi
      fi
      [ -n "${dependency_source}" ] || continue
      [ -f "${dependency_source}" ] || fail "missing dependency: ${dependency_source}"
      dependency_target="${staging_root}/lib/$(basename "${dependency_source}")"
      if [ ! -f "${dependency_target}" ]; then
        cp -Lp "${dependency_source}" "${dependency_target}"
        chmod u+w "${dependency_target}"
        copied=1
      fi
    done < <(otool -L "${candidate}" | sed '1d' | awk '{print $1}')
  done < <(find "${staging_root}/bin" "${staging_root}/lib" -type f -print0)
  [ "${copied}" -eq 1 ] || break
done

while IFS= read -r -d '' candidate; do
  is_macho "${candidate}" || continue
  chmod u+w "${candidate}"
  while IFS= read -r dependency; do
    replacement=""
    if is_vendored_dependency "${dependency}"; then
      dependency_name="$(basename "${dependency}")"
    elif [[ "${dependency}" = @rpath/* ]] &&
         [ -f "${staging_root}/lib/${dependency#@rpath/}" ]; then
      dependency_name="${dependency#@rpath/}"
    else
      continue
    fi
    relative_dependency="$(python3 - "${candidate}" "${staging_root}/lib/${dependency_name}" <<'PY'
import os
import sys
print(os.path.relpath(sys.argv[2], os.path.dirname(sys.argv[1])))
PY
)"
    replacement="@loader_path/${relative_dependency}"
    run_quietly install_name_tool -change \
      "${dependency}" "${replacement}" "${candidate}"
  done < <(otool -L "${candidate}" | sed '1d' | awk '{print $1}')
  case "${candidate}" in
    *.dylib)
      run_quietly install_name_tool -id \
        "@rpath/$(basename "${candidate}")" "${candidate}"
      ;;
  esac
done < <(find "${staging_root}/bin" "${staging_root}/lib" -type f -print0)

# Homebrew bottles carry ad-hoc signatures. Rewriting Mach-O load commands
# invalidates them, and macOS kills those binaries before dyld can report an
# ordinary error. Re-sign each relocated object ad hoc; the enclosing release
# workflow later applies the real Developer ID signature to the app bundle.
while IFS= read -r -d '' candidate; do
  is_macho "${candidate}" || continue
  run_quietly codesign --force --sign - "${candidate}"
done < <(find "${staging_root}/bin" "${staging_root}/lib" -type f -print0)

for tool in postgres initdb pg_ctl pg_dump pg_restore psql; do
  [ -x "${staging_root}/bin/${tool}" ] || fail "relocated runtime is missing ${tool}"
done
find "${staging_root}/share" -type f -name postgres.bki -print -quit |
  grep -q . || fail "relocated runtime is missing postgres.bki"

forbidden="$(
  while IFS= read -r -d '' candidate; do
    is_macho "${candidate}" || continue
    otool -L "${candidate}" | sed '1d' | awk '{print $1}' |
      grep -E '^(/opt/homebrew/|/usr/local/|/opt/local/)' || true
  done < <(find "${staging_root}/bin" "${staging_root}/lib" -type f -print0)
)"
[ -z "${forbidden}" ] || fail "relocated runtime retains package-manager load paths"

rm -rf -- "${destination_root}"
mv "${staging_root}" "${destination_root}"
trap - EXIT
printf '%s\n' "${destination_root}"
