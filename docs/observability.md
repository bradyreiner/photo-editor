# Observability

This application now writes structured diagnostics for each desktop session into a dedicated run
directory under the app log root, but only when development observability is enabled.

- `logs/runs/<timestamp>-<session>/session.json`
- `logs/runs/<timestamp>-<session>/desktop.jsonl`
- `logs/runs/<timestamp>-<session>/<rust-helper>.jsonl`

By default this is enabled for `Debug` builds and disabled for `Release`/`RelWithDebInfo` builds.
The build toggle is `PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY`.

The current implementation is intentionally minimal and vendor-neutral:

- structured JSONL logs instead of ad hoc text lines
- stable categories for app, catalog, jobs, preview, and performance
- per-run correlation via `PHOTO_EDITOR_SESSION_ID`
- helper-process propagation via `PHOTO_EDITOR_OBSERVABILITY_RUN_DIR`
- latency measurements around startup, preview render stages, catalog helpers, and jobs

## Environment

- `PHOTO_EDITOR_LOG_LEVEL`
  - Supported values: `info` (default), `debug`, `trace`, `warning`, `error`
- `PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY`
  - Set to `1` by the desktop app in supported dev builds so helper binaries participate in the same run.
- `PHOTO_EDITOR_SESSION_ID`
  - Optional session correlation id. The desktop app generates one when not provided.
- `PHOTO_EDITOR_OBSERVABILITY_RUN_DIR`
  - Optional override for the current run directory.

## What To Look At

- `session.json`
  - Entry point for the run. Includes the session id, log level, run directory, and desktop log path.
- `desktop.jsonl`
  - Qt-side lifecycle, preview, performance, and job scheduling events.
- `catalog_local_db.jsonl`, `import_catalog.jsonl`, `catalog_migrate.jsonl`
  - Rust helper activity for catalog bootstrap, imports, and migrations.

Each JSON line contains an event name plus timing and context fields. Typical useful filters:

- preview render latency by `event=preview.render.completed`
- startup timing by `event=desktop.startup.ready`
- catalog bootstrap failures by `event=catalog.bootstrap.failed`
- job outcomes by `event=job.completed` or `event=job.failed`

## Current Gaps

This is the baseline layer, not the full observability stack yet. The next upgrades should be:

- crash reporting with native minidumps
- OpenTelemetry metrics and traces exported to a collector
- sampling/profiling for sustained render hotspots
- dashboards and alert thresholds for render latency, job failure rate, and startup regressions
