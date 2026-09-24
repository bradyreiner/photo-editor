#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
postgres_runtime_dir="${PHOTO_EDITOR_POSTGRES_RUNTIME_DIR:-}"
catalog_bin_dir="${PHOTO_EDITOR_CATALOG_BIN_DIR:-${repo_root}/rust/target/release}"
app_binary="${PHOTO_EDITOR_APP_BINARY:-${repo_root}/build/release-macos/apps/desktop/Looksy.app/Contents/MacOS/Looksy}"
source_backup=""
report_path=""
asset_ids=()
asset_id_count=0
plan_only=false
chunk_authorization=""

fail() {
  echo "PostgreSQL rendering migration rehearsal: $*" >&2
  exit 1
}

usage() {
  echo "Usage: $0 --backup /absolute/path/catalog.dump --report /absolute/path/report.json [--asset-id asset-0123456789abcdef ...] [--plan-only | --chunk-authorization /absolute/path/authorization.json]" >&2
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --backup)
      shift
      [ "$#" -gt 0 ] || fail "--backup requires a path"
      source_backup="$1"
      ;;
    --report)
      shift
      [ "$#" -gt 0 ] || fail "--report requires a path"
      report_path="$1"
      ;;
    --asset-id)
      shift
      [ "$#" -gt 0 ] || fail "--asset-id requires a value"
      asset_ids+=("$1")
      asset_id_count=$((asset_id_count + 1))
      ;;
    --plan-only)
      plan_only=true
      ;;
    --chunk-authorization)
      shift
      [ "$#" -gt 0 ] || fail "--chunk-authorization requires a path"
      chunk_authorization="$1"
      ;;
    *)
      usage
      fail "unknown argument: $1"
      ;;
  esac
  shift
done

[ -f "${source_backup}" ] || fail "backup does not exist: ${source_backup}"
[ -n "${report_path}" ] || fail "--report is required"
case "${report_path}" in
  /*) ;;
  *) fail "report path must be absolute" ;;
esac
[ ! -e "${report_path}" ] || fail "report path already exists: ${report_path}"
[ ! -e "${report_path}.assets" ] || fail "report artifact path already exists: ${report_path}.assets"
[ "${plan_only}" = true ] || [ -f "${chunk_authorization}" ] ||
  fail "rendering requires --chunk-authorization from an approved v2 plan"
[ "${plan_only}" = false ] || [ -z "${chunk_authorization}" ] ||
  fail "--plan-only does not accept --chunk-authorization"
[ -x "${app_binary}" ] || fail "missing app binary: ${app_binary}"
[ -x "${catalog_bin_dir}/catalog_migrate" ] ||
  fail "missing catalog_migrate helper: ${catalog_bin_dir}/catalog_migrate"
[ -x "${catalog_bin_dir}/catalog_migration_inventory" ] ||
  fail "missing catalog_migration_inventory helper: ${catalog_bin_dir}/catalog_migration_inventory"
[ -n "${postgres_runtime_dir}" ] ||
  fail "PHOTO_EDITOR_POSTGRES_RUNTIME_DIR must name a PostgreSQL runtime root"
for tool in initdb pg_ctl pg_restore psql; do
  [ -x "${postgres_runtime_dir}/bin/${tool}" ] ||
    fail "missing executable PostgreSQL tool: ${postgres_runtime_dir}/bin/${tool}"
done

asset_sql_list=""
if [ "${asset_id_count}" -gt 0 ]; then
  for asset_id in "${asset_ids[@]}"; do
    [[ "${asset_id}" =~ ^asset-[0-9a-f]{16}$ ]] ||
      fail "invalid asset id: ${asset_id}"
    if [ -n "${asset_sql_list}" ]; then
      asset_sql_list="${asset_sql_list},"
    fi
    asset_sql_list="${asset_sql_list}'${asset_id}'"
  done
fi

proof_root="$(mktemp -d /tmp/framehouse-rendering-migration-rehearsal.XXXXXX)"
data_dir="${proof_root}/data"
log_path="${proof_root}/postgres.log"
database_name="photo_editor_rendering_migration_rehearsal"
port="$(python3 -c \
  'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')"

cleanup() {
  "${postgres_runtime_dir}/bin/pg_ctl" -D "${data_dir}" -m fast stop \
    >/dev/null 2>&1 || true
  case "${proof_root}" in
    /tmp/framehouse-rendering-migration-rehearsal.*)
      rm -rf -- "${proof_root}"
      ;;
  esac
}
trap cleanup EXIT

"${postgres_runtime_dir}/bin/pg_restore" --list "${source_backup}" >/dev/null ||
  fail "backup is not a valid PostgreSQL archive"
"${postgres_runtime_dir}/bin/initdb" \
  -D "${data_dir}" --username=photo_editor --auth=trust >/dev/null
"${postgres_runtime_dir}/bin/pg_ctl" \
  -D "${data_dir}" -l "${log_path}" -w start \
  -o "-c listen_addresses=127.0.0.1 -c port=${port}" >/dev/null
"${postgres_runtime_dir}/bin/psql" \
  -h 127.0.0.1 -p "${port}" -U photo_editor -d postgres \
  -v ON_ERROR_STOP=1 -c "CREATE DATABASE ${database_name}" >/dev/null
"${postgres_runtime_dir}/bin/pg_restore" \
  -h 127.0.0.1 -p "${port}" -U photo_editor -d "${database_name}" \
  --exit-on-error --no-owner --no-privileges "${source_backup}" >/dev/null

database_url="postgresql://photo_editor@127.0.0.1:${port}/${database_name}"
PHOTO_EDITOR_DATABASE_URL="${database_url}" \
PHOTO_EDITOR_APP_DATA_DIR="${proof_root}/app-data" \
PHOTO_EDITOR_LOG_DIR="${proof_root}/logs" \
PHOTO_EDITOR_BACKUP_DIR="${proof_root}/backups" \
  "${catalog_bin_dir}/catalog_migrate" >/dev/null

if [ -n "${asset_sql_list}" ]; then
  "${postgres_runtime_dir}/bin/psql" "${database_url}" \
    -v ON_ERROR_STOP=1 -c \
    "DELETE FROM edit_recipes WHERE asset_id NOT IN (${asset_sql_list});" >/dev/null
fi

selected_count="$("${postgres_runtime_dir}/bin/psql" "${database_url}" -At -c \
  "SELECT count(*) FROM edit_recipes;")"
if [ "${asset_id_count}" -gt 0 ] && [ "${selected_count}" -ne "${asset_id_count}" ]; then
  fail "selected ${asset_id_count} asset ids but disposable catalog contains ${selected_count} recipes"
fi

mkdir -p "$(dirname "${report_path}")" "${proof_root}/recipe-assets"
rendering_arguments=(
  --rendering-migration-report "${report_path}"
  --rendering-migration-recipe-assets "${proof_root}/recipe-assets"
)
if [ "${plan_only}" = true ]; then
  rendering_arguments+=(--rendering-migration-plan-only)
else
  rendering_arguments+=(
    --rendering-migration-chunk-authorization "${chunk_authorization}"
  )
fi
PHOTO_EDITOR_DATABASE_URL="${database_url}" \
PHOTO_EDITOR_APP_DATA_DIR="${proof_root}/app-data" \
PHOTO_EDITOR_LOG_DIR="${proof_root}/logs" \
PHOTO_EDITOR_BACKUP_DIR="${proof_root}/backups" \
  PHOTO_EDITOR_REPOSITORY_ROOT="${repo_root}" \
  "${app_binary}" \
  "${rendering_arguments[@]}"

echo "postgres_rendering_migration_rehearsal_asset_count=${selected_count}"
echo "postgres_rendering_migration_rehearsal=passed"
