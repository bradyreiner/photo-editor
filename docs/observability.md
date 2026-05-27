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
- optional OpenTelemetry traces for Rust helper processes when an OTLP endpoint is configured

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
  - Optional OTLP collector endpoint for Rust helper processes.
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
  - Rust helper binaries emit spans when either OTLP endpoint variable is set.
  - The current desktop Qt/C++ layer still emits JSONL only; OTLP there is a future step.

Each JSON line contains a top-level `event` plus timing and context fields under `fields`.
Desktop and Rust helper records share the same performance field vocabulary where applicable:
`workflow`, `asset_id` or `document_id`, `reason`, `interactive`, `stage`, `elapsed_ms`,
`queue_wait_ms`, `cache_hit`, `bytes`, `thread`, `backend`, and `fallback_reason`.
Older aliases such as `render_reason`, `decoded_cache_hit`, and `linear_cache_hit` may still be
present for compatibility, but the standard aliases are emitted for new records. Typical useful
filters:

- preview render latency by `event=preview.render.completed`
- startup timing by `event=desktop.startup.ready`
- catalog bootstrap failures by `event=catalog.bootstrap.failed`
- import discovery timing by `event=catalog_import.discovery.completed`
- job outcomes by `event=job.completed` or `event=job.failed`

## Summarize Performance Runs

Use the local summarizer to turn a run directory into p50/p95/max timing tables:

```sh
scripts/perf/summarize-jsonl.py logs/runs/<timestamp>-<session>
```

It accepts a single `.jsonl` file, one run directory, the `logs` directory, or the `logs/runs`
directory. By default it summarizes:

- `desktop.startup.ready`
- `preview.render.completed`
- `preview.histogram.completed`
- `thumbnail.completed`
- `job.completed`
- `job.failed`

Useful variants:

```sh
scripts/perf/summarize-jsonl.py logs/runs --all-events
scripts/perf/summarize-jsonl.py logs/runs/<run> --event preview.render.completed --json
scripts/perf/summarize-jsonl.py logs/runs/<current-run> --compare logs/runs/<baseline-run>
```

## Run A Benchmark Artifact

Use the benchmark runner when a change needs a reproducible artifact instead of an ad hoc app
launch:

```sh
scripts/perf/create-preview-corpus.py
scripts/perf/run-benchmark.py --scenario perf-startup
scripts/perf/run-benchmark.py --scenario perf-preview-raster --corpus perf-corpora/preview-synthetic-v1/manifest.json
scripts/perf/run-benchmark.py \
  --scenario perf-preview-raster \
  --app build/dev-macos-debug/tests/preview_benchmark_driver \
  --corpus perf-corpora/preview-synthetic-v1/manifest.json \
  --event benchmark.preview_corpus.completed \
  -- --corpus perf-corpora/preview-synthetic-v1/manifest.json
```

The runner creates an isolated app data root under `perf-artifacts/`, enables development
observability, waits for `desktop.startup.ready` by default, terminates the app unless `--keep-open`
is set, and writes:

- `manifest.json`
- `summary.txt`
- `summary.json`
- `logs/runs/<scenario-session>/desktop.jsonl`
- `runtime/`

Named scenarios are available for startup, library scroll, raster/RAW preview, color adjustment CPU
passes, cold/warm thumbnail generation, mask brush, export batch, timeline scroll/playback, and carousel preview. Scenario-specific
automation can be layered on top of the same artifact contract as those workflows get dedicated
drivers.
`create-preview-corpus.py` creates a deterministic synthetic PNG corpus and recipe manifest for
raster-preview baselines; real JPEG/HEIC/RAW corpora still need local camera files for final sign-off.
`preview_benchmark_driver` is a non-CTest executable that loads that manifest through
`EditorPreviewController` and emits `benchmark.preview_asset.completed`,
`preview.render.completed`, `preview.histogram.completed`, and `benchmark.preview_corpus.completed`.
`thumbnail_benchmark_driver` loads the same manifest through `CatalogAssetModel`, waits for a visible
window of cold thumbnails, then constructs a fresh warm-cache model and emits
`benchmark.thumbnail_corpus.completed` for both passes.

The script understands both current records with a top-level `event` and older desktop records with
`fields.event`. When `--compare` is provided, it prints p50 and p95 deltas for matching event timing
fields.

`preview.render.completed` records also include decoded-cache and linear-cache lookup/hit state,
cache entry and byte totals, the decode backend used for the source image, the render backend used
for adjustments, and the GPU fallback reason when preview falls back to CPU.
Rendered export `job.completed` and `job.failed` records include the same `backend` and
`fallback_reason` fields so export throughput can be grouped by CPU versus Metal eligibility.
Histogram and scope computation now runs as a separate preview-side background task and emits
`preview.histogram.completed` with `elapsed_ms`, sample count, clipping counts, `render_reason`, and
`interaction_mode`.
Rapid repeated preview requests may emit `preview.render.coalesced`, including the number of
follow-up requests folded into the pending interactive render.
Preview stage dispatch emits `preview.render.stage_queued` with `queue_wait_ms`, and completed
preview records include the same queue-wait value.
Preview request, stage, failure, and completion records include `render_reason` and
`interaction_mode` so latency can be grouped by asset selection, edit changes, draft interactive
renders, settled interactive renders, and full-resolution renders.
Background job records emit `queue_wait_ms`, `cancellation_requested`, and `cancellation_reason`.
Thumbnail generation emits `thumbnail.started`, `thumbnail.completed`, and stale-work
`thumbnail.canceled` events with queue wait, elapsed time, cancellation reason, queue priority
(`selected`, `visible_window`, `visible_prefetch`, `filtered`, or `background`), active worker count,
and worker limit fields.
Generated thumbnail sources are versioned with cache-file mtime/size query parameters, allowing hot
QML thumbnail delegates to keep `Image.cache` enabled without holding stale overwritten files.
Catalog import emits `catalog_import.discovery.completed` with candidate and unsupported-file counts
plus discovery elapsed time, and `catalog_import.completed` with separate `discovery_elapsed_ms`,
`database_elapsed_ms`, and total `elapsed_ms` fields.

## Current Gaps

This is still a layered rollout rather than the full observability stack. The main remaining gaps are:

- crash reporting with native minidumps
- OpenTelemetry metrics and traces from the Qt/C++ desktop shell
- sampling/profiling for sustained render hotspots
- dashboards and alert thresholds for render latency, job failure rate, and startup regressions
