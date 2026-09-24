#!/usr/bin/env bash

set -euo pipefail

app_path="${1:-}"
if [ -z "${app_path}" ] || [ ! -d "${app_path}/Contents" ]; then
  printf 'usage: %s /path/to/Looksy.app\n' "${0##*/}" >&2
  exit 2
fi

app_path="$(cd "${app_path}" && pwd -P)"
main_executable="${app_path}/Contents/MacOS/Looksy"
[ -x "${main_executable}" ] || {
  printf '[bundle-dependencies] error: missing main executable: %s\n' "${main_executable}" >&2
  exit 1
}

mach_o_count=0
dependency_count=0
errors=0

append_rpaths() {
  local binary="$1"
  otool -l "${binary}" 2>/dev/null |
    awk '/cmd LC_RPATH/{getline; getline; print $2}'
}

expanded_runtime_path() {
  local runtime_path="$1"
  local loader_dir="$2"
  case "${runtime_path}" in
    @loader_path)
      printf '%s\n' "${loader_dir}"
      ;;
    @loader_path/*)
      printf '%s/%s\n' "${loader_dir}" "${runtime_path#@loader_path/}"
      ;;
    @executable_path)
      printf '%s\n' "${app_path}/Contents/MacOS"
      ;;
    @executable_path/*)
      printf '%s/%s\n' "${app_path}/Contents/MacOS" "${runtime_path#@executable_path/}"
      ;;
    /*)
      printf '%s\n' "${runtime_path}"
      ;;
  esac
}

contained_dependency_exists() {
  local candidate="$1"
  local target directory
  local links=0
  [ -f "${candidate}" ] || return 1
  while [ -L "${candidate}" ]; do
    links=$((links + 1))
    [ "${links}" -le 40 ] || return 1
    target="$(readlink "${candidate}")" || return 1
    case "${target}" in
      /*) candidate="${target}" ;;
      *) candidate="${candidate%/*}/${target}" ;;
    esac
  done
  directory="$(cd "${candidate%/*}" && pwd -P)" || return 1
  candidate="${directory}/${candidate##*/}"
  case "${candidate}" in
    "${app_path}"/*|/System/Library/*|/usr/lib/*) return 0 ;;
    *) return 1 ;;
  esac
}

dependency_exists() {
  local binary="$1"
  local dependency="$2"
  local loader_dir="${binary%/*}"
  local candidate runtime_path

  case "${dependency}" in
    /System/Library/*|/usr/lib/*)
      return 0
      ;;
    @loader_path|@loader_path/*|@executable_path|@executable_path/*)
      candidate="$(expanded_runtime_path "${dependency}" "${loader_dir}")"
      contained_dependency_exists "${candidate}"
      return
      ;;
    @rpath/*)
      while IFS= read -r runtime_path; do
        [ -n "${runtime_path}" ] || continue
        candidate="$(expanded_runtime_path "${runtime_path}" "${loader_dir}")/${dependency#@rpath/}"
        if contained_dependency_exists "${candidate}"; then
          return 0
        fi
      done < <(
        {
          append_rpaths "${binary}"
          if [ "${binary}" != "${main_executable}" ]; then
            append_rpaths "${main_executable}"
          fi
        } | awk '!seen[$0]++'
      )
      return 1
      ;;
    /*)
      return 1
      ;;
    *)
      return 1
      ;;
  esac
}

while IFS= read -r -d '' candidate; do
  if ! file -b "${candidate}" | grep -q 'Mach-O'; then
    continue
  fi
  mach_o_count=$((mach_o_count + 1))
  install_id="$(otool -D "${candidate}" 2>/dev/null | sed -n '2p' || true)"
  while IFS= read -r dependency; do
    [ -n "${dependency}" ] || continue
    if [ "${dependency}" = "${install_id}" ]; then
      continue
    fi
    dependency_count=$((dependency_count + 1))
    if ! dependency_exists "${candidate}" "${dependency}"; then
      printf '[bundle-dependencies] error: %s -> %s is not resolvable inside the app or the macOS system runtime\n' \
        "${candidate#${app_path}/}" "${dependency}" >&2
      errors=$((errors + 1))
    fi
  # Universal binaries have a separate unindented heading for each architecture.
  # Only indented records are dependency names; a second heading is not a load path.
  done < <(otool -L "${candidate}" 2>/dev/null | awk '/^[[:space:]]/ {print $1}')
done < <(find "${app_path}" -type f -print0)

if [ "${mach_o_count}" -eq 0 ]; then
  printf '[bundle-dependencies] error: no Mach-O files found under %s\n' "${app_path}" >&2
  exit 1
fi
if [ "${errors}" -ne 0 ]; then
  printf '[bundle-dependencies] failed mach_o=%d dependencies=%d errors=%d\n' \
    "${mach_o_count}" "${dependency_count}" "${errors}" >&2
  exit 1
fi

printf '[bundle-dependencies] passed mach_o=%d dependencies=%d\n' \
  "${mach_o_count}" "${dependency_count}"
