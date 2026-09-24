#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
catalog_bin_dir="${PHOTO_EDITOR_CATALOG_BIN_DIR:-${repo_root}/rust/target/debug}"
postgres_runtime_dir="${PHOTO_EDITOR_POSTGRES_RUNTIME_DIR:-}"

fail() {
  echo "postgres catalog runtime proof: $*" >&2
  exit 1
}

while IFS= read -r helper; do
  [ -n "${helper}" ] || continue
  [ -x "${catalog_bin_dir}/${helper}" ] ||
    fail "missing executable catalog helper: ${catalog_bin_dir}/${helper}"
done <"${repo_root}/cmake/CatalogHelpers.txt"

[ -n "${postgres_runtime_dir}" ] ||
  fail "PHOTO_EDITOR_POSTGRES_RUNTIME_DIR must name a PostgreSQL runtime root"
for tool in postgres initdb pg_ctl pg_dump pg_restore psql; do
  [ -x "${postgres_runtime_dir}/bin/${tool}" ] ||
    fail "missing executable PostgreSQL tool: ${postgres_runtime_dir}/bin/${tool}"
done

proof_root="$(mktemp -d /tmp/framehouse-postgres-runtime-proof.XXXXXX)"
managed_root="${proof_root}/managed"
external_client_root="${proof_root}/external-client"

cleanup() {
  unset PHOTO_EDITOR_DATABASE_URL
  unset PHOTO_EDITOR_REAL_PG_RESTORE
  export PHOTO_EDITOR_APP_DATA_DIR="${managed_root}/app-data"
  export PHOTO_EDITOR_LOG_DIR="${managed_root}/logs"
  export PHOTO_EDITOR_BACKUP_DIR="${managed_root}/backups"
  "${catalog_bin_dir}/catalog_local_db" stop >/dev/null 2>&1 || true
  case "${proof_root}" in
    /tmp/framehouse-postgres-runtime-proof.*)
      rm -rf -- "${proof_root}"
      ;;
  esac
}
trap cleanup EXIT

unset PHOTO_EDITOR_DATABASE_URL
export PHOTO_EDITOR_POSTGRES_BIN_DIR="${postgres_runtime_dir}/bin"
export PHOTO_EDITOR_APP_DATA_DIR="${managed_root}/app-data"
export PHOTO_EDITOR_LOG_DIR="${managed_root}/logs"
export PHOTO_EDITOR_BACKUP_DIR="${managed_root}/backups"

"${catalog_bin_dir}/catalog_local_db" ensure-running >/dev/null
credential_state_path="${PHOTO_EDITOR_APP_DATA_DIR}/postgres/state.json"
legacy_credential_path="${PHOTO_EDITOR_APP_DATA_DIR}/postgres/.credential"
original_credential="$({
  sed -n 's/^[[:space:]]*"password":[[:space:]]*"\([^"]*\)".*/\1/p' \
    "${credential_state_path}"
} | head -n 1)"
[ -n "${original_credential}" ] ||
  fail "managed state did not contain a recoverable credential"
printf '%s\n' "${original_credential}" >"${legacy_credential_path}"
chmod 600 "${legacy_credential_path}"
"${catalog_bin_dir}/catalog_snapshot" >"${proof_root}/credential-recovery-before.json"
sed 's/"password":[[:space:]]*"[^"]*"/"password": "invalid-replacement-authority"/' \
  "${credential_state_path}" >"${credential_state_path}.replacement"
chmod 600 "${credential_state_path}.replacement"
mv "${credential_state_path}.replacement" "${credential_state_path}"
"${catalog_bin_dir}/catalog_local_db" ensure-running >/dev/null
recovered_credential="$({
  sed -n 's/^[[:space:]]*"password":[[:space:]]*"\([^"]*\)".*/\1/p' \
    "${credential_state_path}"
} | head -n 1)"
[ "${recovered_credential}" = "${original_credential}" ] ||
  fail "managed startup did not restore the proven legacy credential"
"${catalog_bin_dir}/catalog_snapshot" >"${proof_root}/credential-recovery-after.json"
cmp -s "${proof_root}/credential-recovery-before.json" \
  "${proof_root}/credential-recovery-after.json" ||
  fail "credential recovery changed catalog contents"
"${catalog_bin_dir}/catalog_state" create-project RemovedByPreMigrationRestore
find "${PHOTO_EDITOR_BACKUP_DIR}" -type f -name 'pre-migration-v*.dump' -print |
  sort >"${proof_root}/pre-migration-backups.txt"
[ "$(wc -l <"${proof_root}/pre-migration-backups.txt" | tr -d ' ')" = "1" ] ||
  fail "managed schema initialization did not create exactly one pre-migration backup"
pre_migration_backup_path="$(sed -n '1p' "${proof_root}/pre-migration-backups.txt")"
"${postgres_runtime_dir}/bin/pg_restore" --list "${pre_migration_backup_path}" \
  >/dev/null || fail "pre-migration backup is not a valid PostgreSQL archive"
"${catalog_bin_dir}/catalog_local_db" restore-latest >/dev/null
"${catalog_bin_dir}/catalog_snapshot" >"${proof_root}/pre-migration-restored-snapshot.json"
if grep -Fq '"name": "RemovedByPreMigrationRestore"' \
    "${proof_root}/pre-migration-restored-snapshot.json"; then
  fail "pre-migration restore retained a post-backup catalog mutation"
fi
find "${PHOTO_EDITOR_BACKUP_DIR}" -type f -name 'pre-migration-v*.dump' -print |
  sort >"${proof_root}/pre-migration-backups-after-restore.txt"
cmp -s "${proof_root}/pre-migration-backups.txt" \
  "${proof_root}/pre-migration-backups-after-restore.txt" ||
  fail "managed schema startup created duplicate pre-migration backups"
"${catalog_bin_dir}/catalog_state" create-project BeforeBackup
backup_path="$("${catalog_bin_dir}/catalog_local_db" backup)"
[ -f "${backup_path}" ] || fail "managed backup was not created"
"${catalog_bin_dir}/catalog_state" create-project AfterBackup
"${catalog_bin_dir}/catalog_local_db" restore-latest >/dev/null
"${catalog_bin_dir}/catalog_snapshot" >"${proof_root}/restored-snapshot.json"
grep -Fq '"name": "BeforeBackup"' "${proof_root}/restored-snapshot.json" ||
  fail "restore lost the pre-backup project"
if grep -Fq '"name": "AfterBackup"' "${proof_root}/restored-snapshot.json"; then
  fail "restore retained the post-backup mutation"
fi
"${catalog_bin_dir}/catalog_state" create-project SurvivesCorruptRestore
sleep 1
corrupt_backup_path="${PHOTO_EDITOR_BACKUP_DIR}/catalog-manual-corrupt.dump"
printf '%s\n' 'not a PostgreSQL custom archive' >"${corrupt_backup_path}"
if "${catalog_bin_dir}/catalog_local_db" restore-latest \
    >"${proof_root}/corrupt-restore.stdout" \
    2>"${proof_root}/corrupt-restore.stderr"; then
  fail "managed PostgreSQL accepted a corrupt backup archive"
fi
grep -Fq "PostgreSQL backup validation failed" \
  "${proof_root}/corrupt-restore.stderr" ||
  fail "corrupt restore refusal did not report archive validation failure"
"${catalog_bin_dir}/catalog_snapshot" >"${proof_root}/post-corrupt-snapshot.json"
grep -Fq '"name": "SurvivesCorruptRestore"' \
  "${proof_root}/post-corrupt-snapshot.json" ||
  fail "corrupt restore changed the live catalog before validation"
rm -- "${corrupt_backup_path}"
find "${PHOTO_EDITOR_BACKUP_DIR}" -type f -name 'catalog-manual-*.dump' -print |
  sort >"${proof_root}/backups-before-interruption.txt"
failure_bin_dir="${proof_root}/failing-postgres-bin"
mkdir -p "${failure_bin_dir}"
cat >"${failure_bin_dir}/pg_dump" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
partial_path=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --file)
      shift
      partial_path="${1:-}"
      ;;
    --file=*)
      partial_path="${1#--file=}"
      ;;
  esac
  shift
done
[ -n "${partial_path}" ] || exit 41
printf '%s\n' 'interrupted pg_dump output' >"${partial_path}"
exit 42
EOF
chmod 700 "${failure_bin_dir}/pg_dump"
export PHOTO_EDITOR_POSTGRES_BIN_DIR="${failure_bin_dir}"
if "${catalog_bin_dir}/catalog_local_db" backup \
    >"${proof_root}/interrupted-backup.stdout" \
    2>"${proof_root}/interrupted-backup.stderr"; then
  fail "interrupted pg_dump published a completed backup"
fi
grep -Fq "pg_dump failed" "${proof_root}/interrupted-backup.stderr" ||
  fail "interrupted backup did not report the pg_dump failure"
if find "${PHOTO_EDITOR_BACKUP_DIR}" -type f -name '*.partial' -print -quit |
    grep -q .; then
  fail "interrupted pg_dump stranded a partial backup archive"
fi
find "${PHOTO_EDITOR_BACKUP_DIR}" -type f -name 'catalog-manual-*.dump' -print |
  sort >"${proof_root}/backups-after-interruption.txt"
cmp -s "${proof_root}/backups-before-interruption.txt" \
  "${proof_root}/backups-after-interruption.txt" ||
  fail "interrupted pg_dump published a false completed backup"
export PHOTO_EDITOR_POSTGRES_BIN_DIR="${postgres_runtime_dir}/bin"
"${catalog_bin_dir}/catalog_snapshot" >"${proof_root}/post-interruption-snapshot.json"
grep -Fq '"name": "SurvivesCorruptRestore"' \
  "${proof_root}/post-interruption-snapshot.json" ||
  fail "interrupted backup changed the live catalog"

"${catalog_bin_dir}/catalog_state" create-project RestoreRollbackBaseline
sleep 1
restore_rollback_candidate="$("${catalog_bin_dir}/catalog_local_db" backup)"
[ -f "${restore_rollback_candidate}" ] ||
  fail "restore rollback candidate was not created"
"${catalog_bin_dir}/catalog_state" create-project SurvivesFailedRestoreRollback
restore_failure_bin_dir="${proof_root}/failing-restore-postgres-bin"
mkdir -p "${restore_failure_bin_dir}"
ln -s "${postgres_runtime_dir}/bin/pg_dump" "${restore_failure_bin_dir}/pg_dump"
cat >"${restore_failure_bin_dir}/pg_restore" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
real_pg_restore="${PHOTO_EDITOR_REAL_PG_RESTORE:?}"
for argument in "$@"; do
  if [ "${argument}" = "--list" ]; then
    exec "${real_pg_restore}" "$@"
  fi
done
archive_path="${!#}"
case "$(basename "${archive_path}")" in
  pre-restore-*.dump)
    exec "${real_pg_restore}" "$@"
    ;;
esac
"${real_pg_restore}" "$@"
printf '%s\n' 'injected failure after candidate restore' >&2
exit 42
EOF
chmod 700 "${restore_failure_bin_dir}/pg_restore"
export PHOTO_EDITOR_REAL_PG_RESTORE="${postgres_runtime_dir}/bin/pg_restore"
export PHOTO_EDITOR_POSTGRES_BIN_DIR="${restore_failure_bin_dir}"
if "${catalog_bin_dir}/catalog_local_db" restore-latest \
    >"${proof_root}/failed-restore.stdout" \
    2>"${proof_root}/failed-restore.stderr"; then
  fail "injected post-restore failure was reported as successful"
fi
grep -Fq "pre-restore safety backup was reapplied" \
  "${proof_root}/failed-restore.stderr" ||
  fail "failed restore did not report successful safety-backup reapplication"
export PHOTO_EDITOR_POSTGRES_BIN_DIR="${postgres_runtime_dir}/bin"
unset PHOTO_EDITOR_REAL_PG_RESTORE
"${catalog_bin_dir}/catalog_snapshot" >"${proof_root}/post-failed-restore-snapshot.json"
grep -Fq '"name": "RestoreRollbackBaseline"' \
  "${proof_root}/post-failed-restore-snapshot.json" ||
  fail "failed restore rollback lost the candidate baseline"
grep -Fq '"name": "SurvivesFailedRestoreRollback"' \
  "${proof_root}/post-failed-restore-snapshot.json" ||
  fail "failed restore rollback did not recover the pre-restore live catalog"
"${catalog_bin_dir}/catalog_local_db" status >"${proof_root}/managed-status.json"
grep -Fq '"mode": "local-managed"' "${proof_root}/managed-status.json" ||
  fail "managed status reported the wrong authority"
grep -Fq '"healthy": true' "${proof_root}/managed-status.json" ||
  fail "managed PostgreSQL did not report healthy"
grep -Fq '"initialized": true' "${proof_root}/managed-status.json" ||
  fail "managed PostgreSQL did not report initialized"

drive_a_root="${proof_root}/drive-a"
drive_b_root="${proof_root}/drive-b"
relink_root="${proof_root}/relinked"
mkdir -p "${drive_a_root}" "${drive_b_root}" "${relink_root}"
printf '%s\n' 'proof raw a' >"${drive_a_root}/proof-a.dng"
printf '%s\n' 'proof raw b' >"${drive_b_root}/proof-b.dng"
"${catalog_bin_dir}/import_catalog" \
  "${drive_a_root}" proof-drive-a "Proof Drive A" referenced \
  >"${proof_root}/import-drive-a.txt"
"${catalog_bin_dir}/import_catalog" \
  "${drive_a_root}" proof-drive-a "Proof Drive A" referenced \
  >"${proof_root}/import-drive-a-repeat.txt"
"${catalog_bin_dir}/import_catalog" \
  "${drive_b_root}" proof-drive-b "Proof Drive B" referenced \
  >"${proof_root}/import-drive-b.txt"
"${catalog_bin_dir}/catalog_snapshot" >"${proof_root}/multi-drive-snapshot.json"
[ "$(grep -c '"asset_id":' "${proof_root}/multi-drive-snapshot.json")" = "2" ] ||
  fail "repeat import duplicated a stable referenced asset"
[ "$(grep -c '"display_name":' "${proof_root}/multi-drive-snapshot.json")" = "2" ] ||
  fail "catalog did not retain both referenced drive authorities"
drive_a_asset_id="$(awk '
  /"asset_id":/ {
    value = $0
    sub(/^.*"asset_id": "/, "", value)
    sub(/".*$/, "", value)
    asset_id = value
  }
  /"drive_id": "proof-drive-a"/ {
    print asset_id
    exit
  }
' "${proof_root}/multi-drive-snapshot.json")"
[ -n "${drive_a_asset_id}" ] || fail "could not resolve the first drive asset identity"
"${catalog_bin_dir}/catalog_state" set-edit-recipe-v13 \
  "${drive_a_asset_id}" \
  photo-editor-edit-recipe-v13.1 \
  sha256:postgres-runtime-proof \
  '{"schema":"photo-editor-edit-recipe-provenance:v1","presetApplications":[]}'
rm -- "${drive_a_root}/proof-a.dng"
"${catalog_bin_dir}/catalog_migration_inventory" \
  >"${proof_root}/missing-media-inventory.json"
grep -Fq '"edited_asset_count": 1' "${proof_root}/missing-media-inventory.json" ||
  fail "migration inventory did not include the edited missing-media asset"
grep -Fq '"source_available": false' "${proof_root}/missing-media-inventory.json" ||
  fail "missing referenced media was not reported unavailable"
relinked_path="${relink_root}/proof-a-relinked.dng"
printf '%s\n' 'proof raw a recovered' >"${relinked_path}"
"${catalog_bin_dir}/catalog_state" set-relink-path \
  "${drive_a_asset_id}" "${relinked_path}"
"${catalog_bin_dir}/catalog_migration_inventory" \
  >"${proof_root}/relinked-media-inventory.json"
grep -Fq '"source_available": true' "${proof_root}/relinked-media-inventory.json" ||
  fail "relinked referenced media was not reported available"
grep -Fq "${relinked_path}" "${proof_root}/relinked-media-inventory.json" ||
  fail "migration inventory did not use the authoritative relink path"

managed_card_root="${proof_root}/managed-card"
managed_library_before="${proof_root}/managed-library-before"
managed_library_after="${proof_root}/managed-library-after"
mkdir -p "${managed_card_root}" "${managed_library_before}"
printf '%s\n' 'managed proof raw' >"${managed_card_root}/managed-proof.dng"
"${catalog_bin_dir}/import_catalog" \
  "${managed_card_root}" proof-managed-card "Managed Proof Card" managed \
  - "${managed_library_before}" \
  >"${proof_root}/import-managed.txt"
managed_before_path="${managed_library_before}/proof-managed-card/managed-proof.dng"
[ -f "${managed_before_path}" ] || fail "managed import did not copy its original"
"${catalog_bin_dir}/catalog_move_library" \
  "${managed_library_before}" "${managed_library_after}" \
  >"${proof_root}/managed-library-move.json"
managed_after_path="${managed_library_after}/proof-managed-card/managed-proof.dng"
[ -f "${managed_after_path}" ] || fail "library move did not publish the copied original"
[ -f "${managed_before_path}" ] || fail "library move removed the old original before approval"
cmp -s "${managed_before_path}" "${managed_after_path}" ||
  fail "library move changed managed-original bytes"
grep -Fq '"oldLibraryRetained":true' "${proof_root}/managed-library-move.json" ||
  fail "library move did not report old-library retention"
find "${PHOTO_EDITOR_BACKUP_DIR}" -type f -name 'pre-library-move-*.dump' -print -quit |
  grep -q . || fail "library move did not create its PostgreSQL safety backup"
"${catalog_bin_dir}/catalog_snapshot" >"${proof_root}/managed-library-snapshot.json"
grep -Fq "${managed_library_after}/proof-managed-card" \
  "${proof_root}/managed-library-snapshot.json" ||
  fail "catalog did not adopt the relocated managed-library root"
managed_library_failed="${proof_root}/managed-library-failed"
rm -- "${managed_after_path}"
if "${catalog_bin_dir}/catalog_move_library" \
    "${managed_library_after}" "${managed_library_failed}" \
    >"${proof_root}/failed-library-move.stdout" \
    2>"${proof_root}/failed-library-move.stderr"; then
  fail "library move accepted an unavailable managed original"
fi
grep -Fq "managed original is unavailable" \
  "${proof_root}/failed-library-move.stderr" ||
  fail "failed library move did not identify the missing managed original"
[ -d "${managed_library_failed}" ] ||
  fail "failed library move did not preserve the selected empty destination"
if find "${managed_library_failed}" -mindepth 1 -print -quit | grep -q .; then
  fail "failed library move published partial destination content"
fi
if find "${proof_root}" -maxdepth 1 \
    -name '.managed-library-failed.framehouse-library-migration-*' \
    -print -quit | grep -q .; then
  fail "failed library move stranded its sibling staging directory"
fi
"${catalog_bin_dir}/catalog_snapshot" \
  >"${proof_root}/failed-library-move-snapshot.json"
grep -Fq "${managed_library_after}/proof-managed-card" \
  "${proof_root}/failed-library-move-snapshot.json" ||
  fail "failed library move changed catalog root authority"
cp "${managed_before_path}" "${managed_after_path}"

external_database_url="$(
  "${catalog_bin_dir}/catalog_local_db" connection-url --ensure-running
)"
"${postgres_runtime_dir}/bin/psql" "${external_database_url}" \
  --set=ON_ERROR_STOP=1 >/dev/null <<'SQL'
ALTER TABLE assets VALIDATE CONSTRAINT assets_capture_date_range_check;
ALTER TABLE assets VALIDATE CONSTRAINT assets_iso_range_check;
ALTER TABLE assets VALIDATE CONSTRAINT assets_aperture_range_check;
ALTER TABLE assets VALIDATE CONSTRAINT assets_rating_range_check;
ALTER TABLE asset_review_states VALIDATE CONSTRAINT asset_review_states_rating_range_check;
ALTER TABLE asset_review_states VALIDATE CONSTRAINT asset_review_states_flag_exclusivity_check;
ALTER TABLE export_jobs VALIDATE CONSTRAINT export_jobs_progress_range_check;
ALTER TABLE timeline_documents VALIDATE CONSTRAINT timeline_documents_duration_range_check;
ALTER TABLE carousel_documents VALIDATE CONSTRAINT carousel_documents_slide_count_range_check;
ALTER TABLE recipe_migration_records
  VALIDATE CONSTRAINT recipe_migration_records_profile_identities_array_check;
SQL
PHOTO_EDITOR_DATABASE_URL="${external_database_url}" \
  "${catalog_bin_dir}/catalog_integrity" --require-clean \
  >"${proof_root}/clean-catalog-integrity-report.json"

fixture_database="photo_editor_corrupt_fixture"
fixture_database_url="${external_database_url%/*}/${fixture_database}"
"${postgres_runtime_dir}/bin/psql" "${external_database_url}" \
  --set=ON_ERROR_STOP=1 \
  --command="CREATE DATABASE ${fixture_database}" >/dev/null
PHOTO_EDITOR_DATABASE_URL="${fixture_database_url}" \
  "${catalog_bin_dir}/catalog_migrate" >/dev/null

"${postgres_runtime_dir}/bin/psql" "${fixture_database_url}" \
  --set=ON_ERROR_STOP=1 >/dev/null <<'SQL'
INSERT INTO drives (drive_id, display_name, mount_path)
VALUES ('corrupt-fixture-drive', 'Corrupt Fixture Drive', '/fixture');

INSERT INTO assets (
  asset_id, drive_id, root_path, relative_path, import_mode,
  capture_year, capture_month, capture_day,
  camera_make, camera_model, lens_model, iso, shutter_speed, aperture
) VALUES (
  'corrupt-asset', 'corrupt-fixture-drive', '/fixture', 'corrupt.dng', 'referenced',
  2024, 1, 1, NULL, NULL, NULL, 100, NULL, 4.0
);

ALTER TABLE assets DROP CONSTRAINT assets_capture_date_range_check;
UPDATE assets SET capture_year = NULL WHERE asset_id = 'corrupt-asset';
SQL

expect_catalog_integrity_error() {
  local proof_name="$1"
  local identity="$2"
  shift 2
  if PHOTO_EDITOR_DATABASE_URL="${fixture_database_url}" "$@" \
      >"${proof_root}/${proof_name}.stdout" \
      2>"${proof_root}/${proof_name}.stderr"; then
    fail "${proof_name} accepted a corrupt catalog record"
  fi
  grep -Fq "catalog data integrity error" "${proof_root}/${proof_name}.stderr" ||
    fail "${proof_name} did not return a structured integrity error"
  grep -Fq "${identity}" "${proof_root}/${proof_name}.stderr" ||
    fail "${proof_name} did not identify the corrupt record"
  if grep -Eq 'panicked at|thread .* panicked' "${proof_root}/${proof_name}.stderr"; then
    fail "${proof_name} panicked while decoding corrupt catalog data"
  fi
}

expect_catalog_integrity_error corrupt-null-date corrupt-asset \
  "${catalog_bin_dir}/catalog_snapshot"
"${postgres_runtime_dir}/bin/psql" "${fixture_database_url}" \
  --set=ON_ERROR_STOP=1 --command="UPDATE assets SET capture_year = 2024, capture_month = 13 WHERE asset_id = 'corrupt-asset'" >/dev/null
expect_catalog_integrity_error corrupt-month corrupt-asset \
  "${catalog_bin_dir}/catalog_snapshot"
"${postgres_runtime_dir}/bin/psql" "${fixture_database_url}" \
  --set=ON_ERROR_STOP=1 >/dev/null <<'SQL'
UPDATE assets SET capture_month = 1 WHERE asset_id = 'corrupt-asset';
ALTER TABLE assets DROP CONSTRAINT assets_import_mode_check;
UPDATE assets SET import_mode = 'unknown-mode' WHERE asset_id = 'corrupt-asset';
SQL
expect_catalog_integrity_error corrupt-import-mode corrupt-asset \
  "${catalog_bin_dir}/catalog_snapshot"
"${postgres_runtime_dir}/bin/psql" "${fixture_database_url}" \
  --set=ON_ERROR_STOP=1 >/dev/null <<'SQL'
UPDATE assets SET import_mode = 'referenced' WHERE asset_id = 'corrupt-asset';
ALTER TABLE asset_review_states DROP CONSTRAINT asset_review_states_rating_range_check;
INSERT INTO asset_review_states (asset_id, rating) VALUES ('corrupt-asset', 6);
SQL
expect_catalog_integrity_error corrupt-review corrupt-asset \
  "${catalog_bin_dir}/catalog_snapshot"
"${postgres_runtime_dir}/bin/psql" "${fixture_database_url}" \
  --set=ON_ERROR_STOP=1 >/dev/null <<'SQL'
DELETE FROM asset_review_states WHERE asset_id = 'corrupt-asset';
ALTER TABLE asset_relinks ALTER COLUMN relinked_file_path DROP NOT NULL;
INSERT INTO asset_relinks (asset_id, relinked_file_path) VALUES ('corrupt-asset', NULL);
SQL
expect_catalog_integrity_error corrupt-relink corrupt-asset \
  "${catalog_bin_dir}/catalog_snapshot"
"${postgres_runtime_dir}/bin/psql" "${fixture_database_url}" \
  --set=ON_ERROR_STOP=1 >/dev/null <<'SQL'
DELETE FROM asset_relinks WHERE asset_id = 'corrupt-asset';
ALTER TABLE recipe_migration_records
  DROP CONSTRAINT recipe_migration_records_profile_identities_array_check;
INSERT INTO recipe_migration_records (
  asset_id, source_recipe_version, source_renderer, target_renderer,
  profile_identities, plan_hash, evidence_hash, rollback_pointer, status
) VALUES (
  'corrupt-asset', 12, 'legacy', 'current', '{}'::jsonb,
  'plan', 'evidence', 'backup', 'candidate'
);
SQL
expect_catalog_integrity_error corrupt-migration corrupt-asset \
  "${catalog_bin_dir}/catalog_migration_inventory"

"${postgres_runtime_dir}/bin/psql" "${fixture_database_url}" \
  --set=ON_ERROR_STOP=1 >/dev/null <<'SQL'
ALTER TABLE export_jobs DROP CONSTRAINT export_jobs_progress_range_check;
INSERT INTO export_jobs (job_id, preset_name, destination_path, status, progress)
VALUES ('corrupt-export', 'JPEG', '/fixture', 'Queued', 101);

ALTER TABLE timeline_documents DROP CONSTRAINT timeline_documents_duration_range_check;
INSERT INTO timeline_documents (timeline_id, asset_id, duration_seconds)
VALUES ('corrupt-timeline', 'corrupt-timeline-asset', -1);

ALTER TABLE carousel_documents DROP CONSTRAINT carousel_documents_slide_count_range_check;
INSERT INTO carousel_documents (document_id, slide_count)
VALUES ('corrupt-carousel', -1);
SQL
PHOTO_EDITOR_DATABASE_URL="${fixture_database_url}" \
  "${catalog_bin_dir}/catalog_integrity" \
  >"${proof_root}/catalog-integrity-report.json"
for identity in corrupt-asset corrupt-export corrupt-timeline corrupt-carousel; do
  grep -Fq "${identity}" "${proof_root}/catalog-integrity-report.json" ||
    fail "integrity inventory hid corrupt record ${identity}"
done
grep -Fq '"scanned_record_count": 6' \
  "${proof_root}/catalog-integrity-report.json" ||
  fail "integrity inventory did not account for unaffected records"
if PHOTO_EDITOR_DATABASE_URL="${fixture_database_url}" \
    "${catalog_bin_dir}/catalog_integrity" --require-clean \
    >"${proof_root}/catalog-integrity-require-clean.stdout" \
    2>"${proof_root}/catalog-integrity-require-clean.stderr"; then
  fail "integrity release gate accepted corrupt records"
fi
expect_catalog_integrity_error corrupt-export corrupt-export \
  "${catalog_bin_dir}/catalog_export_jobs" list
expect_catalog_integrity_error corrupt-timeline corrupt-timeline \
  "${catalog_bin_dir}/catalog_timeline_documents" list
expect_catalog_integrity_error corrupt-carousel corrupt-carousel \
  "${catalog_bin_dir}/catalog_carousel_documents" list

export PHOTO_EDITOR_DATABASE_URL="${external_database_url}"
export PHOTO_EDITOR_APP_DATA_DIR="${external_client_root}/app-data"
export PHOTO_EDITOR_LOG_DIR="${external_client_root}/logs"
export PHOTO_EDITOR_BACKUP_DIR="${external_client_root}/backups"
"${catalog_bin_dir}/catalog_local_db" ensure-running >/dev/null
"${catalog_bin_dir}/catalog_local_db" status >"${proof_root}/external-status.json"
grep -Fq '"mode": "external"' "${proof_root}/external-status.json" ||
  fail "explicit PostgreSQL URL did not select external mode"
grep -Fq '"healthy": true' "${proof_root}/external-status.json" ||
  fail "external PostgreSQL did not report healthy"
[ ! -e "${external_client_root}/app-data/postgres" ] ||
  fail "external mode created an unauthorized managed cluster"
external_backup_path="$("${catalog_bin_dir}/catalog_local_db" backup)"
[ -f "${external_backup_path}" ] ||
  fail "external PostgreSQL backup was not created"
if "${catalog_bin_dir}/catalog_local_db" restore-latest \
    >"${proof_root}/external-restore.stdout" \
    2>"${proof_root}/external-restore.stderr"; then
  fail "external PostgreSQL accepted an unauthorized automatic restore"
fi
grep -Fq "automatic restore is disabled for an external PostgreSQL server" \
  "${proof_root}/external-restore.stderr" ||
  fail "external restore refusal did not explain the administrator boundary"

unset PHOTO_EDITOR_DATABASE_URL
export PHOTO_EDITOR_APP_DATA_DIR="${managed_root}/app-data"
export PHOTO_EDITOR_LOG_DIR="${managed_root}/logs"
export PHOTO_EDITOR_BACKUP_DIR="${managed_root}/backups"
"${catalog_bin_dir}/catalog_local_db" stop >/dev/null

echo "postgres_catalog_runtime_proof=passed"
echo "postgres_catalog_credential_recovery=passed"
echo "postgres_catalog_pre_migration_rollback=passed"
echo "postgres_catalog_managed_backup_restore=passed"
echo "postgres_catalog_corrupt_restore_guard=passed"
echo "postgres_catalog_interrupted_backup_guard=passed"
echo "postgres_catalog_failed_restore_rollback=passed"
echo "postgres_catalog_media_relink_dedup=passed"
echo "postgres_catalog_managed_library_move=passed"
echo "postgres_catalog_external_authority=passed"
echo "postgres_catalog_external_restore_guard=passed"
echo "postgres_catalog_corrupt_decode_guard=passed"
