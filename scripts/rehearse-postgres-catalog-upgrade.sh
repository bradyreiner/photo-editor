#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
catalog_bin_dir="${PHOTO_EDITOR_CATALOG_BIN_DIR:-${repo_root}/rust/target/debug}"
postgres_runtime_dir="${PHOTO_EDITOR_POSTGRES_RUNTIME_DIR:-}"
source_backup=""

fail() {
  echo "PostgreSQL catalog upgrade rehearsal: $*" >&2
  exit 1
}

usage() {
  echo "Usage: $0 --backup /absolute/path/to/catalog.dump" >&2
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --backup)
      shift
      [ "$#" -gt 0 ] || fail "--backup requires a path"
      source_backup="$1"
      ;;
    *)
      usage
      fail "unknown argument: $1"
      ;;
  esac
  shift
done

[ -f "${source_backup}" ] || fail "backup does not exist: ${source_backup}"
[ -n "${postgres_runtime_dir}" ] ||
  fail "PHOTO_EDITOR_POSTGRES_RUNTIME_DIR must name a PostgreSQL runtime root"

for helper in catalog_migrate catalog_migration_inventory; do
  [ -x "${catalog_bin_dir}/${helper}" ] ||
    fail "missing executable catalog helper: ${catalog_bin_dir}/${helper}"
done
for tool in initdb pg_ctl pg_restore psql; do
  [ -x "${postgres_runtime_dir}/bin/${tool}" ] ||
    fail "missing executable PostgreSQL tool: ${postgres_runtime_dir}/bin/${tool}"
done
command -v jq >/dev/null || fail "jq is required"
command -v python3 >/dev/null || fail "python3 is required"

proof_root="$(mktemp -d /tmp/framehouse-catalog-upgrade-rehearsal.XXXXXX)"
data_dir="${proof_root}/data"
log_path="${proof_root}/postgres.log"
database_name="photo_editor_upgrade_rehearsal"
port="$(python3 -c \
  'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')"

cleanup() {
  "${postgres_runtime_dir}/bin/pg_ctl" -D "${data_dir}" -m fast stop \
    >/dev/null 2>&1 || true
  case "${proof_root}" in
    /tmp/framehouse-catalog-upgrade-rehearsal.*)
      rm -rf -- "${proof_root}"
      ;;
  esac
}
trap cleanup EXIT

count_query="SELECT json_build_object(
  'assets', (SELECT count(*) FROM assets),
  'projects', (SELECT count(*) FROM projects),
  'project_assets', (SELECT count(*) FROM project_assets),
  'edit_recipes', (SELECT count(*) FROM edit_recipes),
  'review_states', (SELECT count(*) FROM asset_review_states),
  'relinks', (SELECT count(*) FROM asset_relinks),
  'drives', (SELECT count(*) FROM drives),
  'timeline_documents', (SELECT count(*) FROM timeline_documents),
  'carousel_documents', (SELECT count(*) FROM carousel_documents),
  'export_jobs', (SELECT count(*) FROM export_jobs));"

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
  --exit-on-error --no-owner --no-privileges "${source_backup}"

database_url="postgresql://photo_editor@127.0.0.1:${port}/${database_name}"
pre_counts="$("${postgres_runtime_dir}/bin/psql" "${database_url}" -At \
  -c "${count_query}")"

PHOTO_EDITOR_DATABASE_URL="${database_url}" \
PHOTO_EDITOR_APP_DATA_DIR="${proof_root}/app-data" \
PHOTO_EDITOR_LOG_DIR="${proof_root}/logs" \
PHOTO_EDITOR_BACKUP_DIR="${proof_root}/backups" \
  "${catalog_bin_dir}/catalog_migrate" >/dev/null

post_counts="$("${postgres_runtime_dir}/bin/psql" "${database_url}" -At \
  -c "${count_query}")"
[ "$(printf '%s' "${pre_counts}" | jq -cS .)" = \
  "$(printf '%s' "${post_counts}" | jq -cS .)" ] ||
  fail "catalog row counts changed while applying migrations"

migration_state="$("${postgres_runtime_dir}/bin/psql" "${database_url}" -At -c \
  "SELECT json_build_object('count', count(*), 'minimum', min(version),
    'maximum', max(version), 'all_success', bool_and(success))
   FROM _sqlx_migrations;")"
[ "$(printf '%s' "${migration_state}" | jq -r '.maximum')" = "14" ] ||
  fail "upgrade did not reach schema migration 14"
[ "$(printf '%s' "${migration_state}" | jq -r '.all_success')" = "true" ] ||
  fail "one or more schema migrations reported failure"

PHOTO_EDITOR_DATABASE_URL="${database_url}" \
PHOTO_EDITOR_APP_DATA_DIR="${proof_root}/app-data" \
PHOTO_EDITOR_LOG_DIR="${proof_root}/logs" \
PHOTO_EDITOR_BACKUP_DIR="${proof_root}/backups" \
  "${catalog_bin_dir}/catalog_migration_inventory" \
  >"${proof_root}/migration-inventory.json"
inventory_summary="$(jq -c '{
    edited_asset_count,
    source_available: ([.assets[] | select(.source_available)] | length),
    source_missing: ([.assets[] | select(.source_available | not)] | length),
    canonical_v13: ([.assets[] |
      select(.recipe_authority == "canonical-v13")] | length),
    legacy_compatibility: ([.assets[] |
      select(.recipe_authority == "legacy-compatibility")] | length),
    renderers: ([.assets[].source_renderer] | group_by(.) |
      map({renderer: .[0], count: length}))
  }' "${proof_root}/migration-inventory.json")"

"${postgres_runtime_dir}/bin/psql" \
  -h 127.0.0.1 -p "${port}" -U photo_editor -d postgres \
  -v ON_ERROR_STOP=1 -c "DROP DATABASE ${database_name}" >/dev/null
"${postgres_runtime_dir}/bin/psql" \
  -h 127.0.0.1 -p "${port}" -U photo_editor -d postgres \
  -v ON_ERROR_STOP=1 -c "CREATE DATABASE ${database_name}" >/dev/null
"${postgres_runtime_dir}/bin/pg_restore" \
  -h 127.0.0.1 -p "${port}" -U photo_editor -d "${database_name}" \
  --exit-on-error --no-owner --no-privileges "${source_backup}"

rollback_counts="$("${postgres_runtime_dir}/bin/psql" "${database_url}" -At \
  -c "${count_query}")"
[ "$(printf '%s' "${pre_counts}" | jq -cS .)" = \
  "$(printf '%s' "${rollback_counts}" | jq -cS .)" ] ||
  fail "restored pre-upgrade catalog row counts do not match"
rollback_schema_max="$("${postgres_runtime_dir}/bin/psql" \
  "${database_url}" -At -c "SELECT max(version) FROM _sqlx_migrations;")"
[ "${rollback_schema_max}" = "9" ] ||
  fail "rollback archive did not restore schema migration 9"

printf 'pre_counts=%s\n' "${pre_counts}"
printf 'post_counts=%s\n' "${post_counts}"
printf 'migration_state=%s\n' "${migration_state}"
printf 'inventory_summary=%s\n' "${inventory_summary}"
printf 'rollback_counts=%s\n' "${rollback_counts}"
printf 'rollback_schema_max=%s\n' "${rollback_schema_max}"
echo "postgres_catalog_real_backup_upgrade_rehearsal=passed"
echo "postgres_catalog_real_backup_rollback_rehearsal=passed"
