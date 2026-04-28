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
- optional OpenTelemetry traces for Rust helper processes and the MCP server when an OTLP endpoint is configured

## Environment

- `PHOTO_EDITOR_LOG_LEVEL`
  - Supported values: `info` (default), `debug`, `trace`, `warning`, `error`
- `PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY`
  - Set to `1` by the desktop app in supported dev builds so helper binaries participate in the same run.
- `PHOTO_EDITOR_SESSION_ID`
  - Optional session correlation id. The desktop app generates one when not provided.
- `PHOTO_EDITOR_OBSERVABILITY_RUN_DIR`
  - Optional override for the current run directory.
- `OTEL_EXPORTER_OTLP_ENDPOINT`
  - Optional OTLP collector endpoint for Rust helper processes and `photo-editor-mcp`.
- `OTEL_EXPORTER_OTLP_TRACES_ENDPOINT`
  - Optional traces-only endpoint. Takes precedence over `OTEL_EXPORTER_OTLP_ENDPOINT`.
- `OTEL_SERVICE_NAMESPACE`
  - Optional namespace attached to emitted traces. A good default for local development is `photo-editor-dev`.
- `OTEL_DEPLOYMENT_ENVIRONMENT`
  - Optional environment label such as `development`, `staging`, or `ci`.

## What To Look At

- `session.json`
  - Entry point for the run. Includes the session id, log level, run directory, and desktop log path.
- `desktop.jsonl`
  - Qt-side lifecycle, preview, performance, and job scheduling events.
- `catalog_local_db.jsonl`, `import_catalog.jsonl`, `catalog_migrate.jsonl`
  - Rust helper activity for catalog bootstrap, imports, and migrations.
- OTLP traces
  - `photo-editor-mcp` and Rust helper binaries emit spans when either OTLP endpoint variable is set.
  - The current desktop Qt/C++ layer still emits JSONL only; OTLP there is a future step.

Each JSON line contains an event name plus timing and context fields. Typical useful filters:

- preview render latency by `event=preview.render.completed`
- startup timing by `event=desktop.startup.ready`
- catalog bootstrap failures by `event=catalog.bootstrap.failed`
- job outcomes by `event=job.completed` or `event=job.failed`

## Current Gaps

This is still a layered rollout rather than the full observability stack. The main remaining gaps are:

- crash reporting with native minidumps
- OpenTelemetry metrics and traces from the Qt/C++ desktop shell
- sampling/profiling for sustained render hotspots
- dashboards and alert thresholds for render latency, job failure rate, and startup regressions
