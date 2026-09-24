# Catalog integrity contract

This document is the canonical inventory for PostgreSQL values selected by the
catalog persistence boundary. The runtime schema is owned by
`rust/crates/catalog/migrations`; `docs/postgres-schema.sql` is the reviewed
post-migration snapshot. All row decoding is owned by the fallible mapping
functions in `rust/crates/catalog/src/lib.rs`.

## Nullability and decoding inventory

| Record | Selected values | Database contract | Runtime contract |
| --- | --- | --- | --- |
| Asset and location | `asset_id`, `drive_id`, `root_path`, `relative_path`, `import_mode`, `is_online` | Not null; import mode is constrained | Required typed values; unknown import modes are integrity errors |
| Asset capture date | `capture_year`, `capture_month`, `capture_day` | Legacy-nullable; migration 0013 rejects null/out-of-range values on new writes | Decode as `Option<i32>`, then require a valid Gregorian date and checked unsigned conversion |
| Asset metadata | `camera_make`, `camera_model`, `lens_model`, `shutter_speed` | Legacy-nullable | Decode as `Option<String>`; absence has the documented public representation `""` |
| Asset exposure | `iso`, `aperture` | Nullable; positive-value constraints on new writes | `Option<i32>` / `Option<f64>`; non-positive, NaN, and infinite values are integrity errors |
| Snapshot recipe | `recipe_json` | Optional left join | `Option<Value>` |
| Canonical recipe | payload, cache key, provenance, updated time | Entire joined record is optional; fields are otherwise not null and schema-constrained | No record when payload is null; a present record requires every typed field |
| Snapshot import time | formatted `created_at` | Not null | Required string |
| Drive | ID, display name, mount path, online state | Not null | Required typed values |
| Project | name, formatted creation time, aggregated asset IDs | Not null; aggregation is always a JSON array | Array must contain only strings; malformed shape or elements are integrity errors |
| Review state | asset ID, rating, picked, rejected, color label | Not null; migration 0013 bounds rating and makes flags exclusive | Rating must be 0-5 and picked/rejected cannot both be true |
| Relink | asset ID, path | Not null | Path is decoded through `Option<String>` so a legacy null reports the asset ID |
| Export job | all job strings, optional recipe/metadata JSON, progress, timestamps | Strings/progress/times not null; recipe and metadata optional | Typed values; progress must be 0-100 |
| Timeline document | IDs, paths, duration, optional JSON, timestamps | Required except selected JSON; migration 0013 bounds duration | Duration must be finite and non-negative |
| Carousel document | ID, name, format, slide count, JSON, timestamps | Not null; migration 0013 bounds slide count | Slide count must be non-negative |
| Recipe migration | IDs/renderers/hashes/status/times, optional approval, JSON profiles | Approval is nullable; status and required strings constrained; profiles must be an array on new writes | Approval is `Option<String>`; profile array must contain only strings |

No persistence mapper uses `Row::get`. Type, nullability, semantic range, JSON
shape, and enum failures propagate as `sqlx::Error` instead of panicking or
being clamped into plausible data.

## Constraint rollout

Migration `0013_catalog_integrity_constraints.sql` adds bounded-value and JSON
shape constraints with `NOT VALID`. PostgreSQL enforces these constraints for
new and updated rows immediately. The modifier intentionally avoids turning a
legacy corrupt row into an anonymous startup failure; the fallible decoder can
instead report the record identity and field.

Before a release candidate, operators must inventory and repair or restore any
legacy violations and then execute `ALTER TABLE ... VALIDATE CONSTRAINT` for
every constraint introduced by migration 0013. A release evidence bundle must
record the database identity, migration version, validation commands, results,
and a verified backup created before repair. Never delete or coerce a corrupt
record merely to pass validation.

Run `catalog_integrity` to emit the complete JSON inventory without passing
aggregate records through the application mappers. It reports every finding and
the total record count, so one corrupt row cannot conceal healthy rows. Run
`catalog_integrity --require-clean` as the release gate; it preserves the JSON
report but returns a failing process status when any finding exists.

## Failure and recovery behavior

Every catalog binary runs through the shared `run_catalog_helper` boundary. A
normal error preserves its sanitized diagnostic and exits unsuccessfully. An
unexpected panic is caught at the process boundary, emits no panic payload, and
directs the operator to owner-only logs or verified backup recovery. PostgreSQL
credentials in connection URLs are redacted before errors reach stderr.

`scripts/test-postgres-catalog-runtime.sh` and its packaged-Windows PowerShell
equivalent build an isolated migrated database, inject representative legacy
corruption after temporarily removing the new constraints, and prove the
snapshot, review/relink, export, timeline, carousel, and rendering-migration
paths fail with a structured record identity rather than a panic. The same run
also re-proves backup validation, interrupted backup cleanup, failed-restore
rollback, managed-library movement, and external-server authority boundaries.
