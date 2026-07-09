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
- `PHOTO_EDITOR_OBSERVABILITY_CONSOLE`
  - Supported values: `compact` (default), `json`, `quiet`, `over_budget`
  - `compact` keeps the terminal readable by printing a small performance summary for structured events.
  - `json` preserves the previous terminal behavior and prints the full structured message.
  - `quiet` prints warnings/errors only.
  - `over_budget` prints warnings/errors plus structured events that failed, cancelled, or exceeded
    their latency budget.
- `PHOTO_EDITOR_LOG_COLOR`
  - Supported values: `auto` (default), `always`, `never`
  - `auto` colorizes local terminal output, but respects `NO_COLOR` and `TERM=dumb`.
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

Correlation fields are stable across app workflows:

- `workflow` is the low-cardinality workflow or benchmark scenario, such as `preview`, `mask`,
  `thumbnail`, `qml_route`, `qml_frame`, `perf-preview-raster`, or `perf-mask-brush`.
- `workflow_id` is the stable run-local correlation key for a workflow instance. Benchmark runs use
  `<scenario>:<session>`, QML routes use `qml-route:<surface>`, frame summaries use
  `qml-frame:<surface>`, and render/tool interactions reuse the active interaction when available.
- `interaction_id` is the user-gesture or tool-operation correlation key. It must stay stable
  across started/stage/completed events and related renders for the same interaction.
- `tool_id` is the normalized tool or command family, such as `global_sliders`, `color_tools`,
  `tone_curves`, `crop_geometry`, `layer_adjustments`, `retouch`, `compare_view`,
  `mask_brush`, `mask_lasso`, `mask_smart_select`, `mask_refine_edge`, or `mask_inpaint`.
- `surface` is the UI or subsystem surface that owns the work, such as `library`, `import`, `edit`,
  `masks`, `export`, `timeline`, `carousel`, `benchmark`, or `worker`.

- preview render latency by `event=preview.render.completed`
- startup timing by `event=desktop.startup.ready`
- catalog bootstrap failures by `event=catalog.bootstrap.failed`
- import discovery timing by `event=catalog_import.discovery.completed`
- job outcomes by `event=job.completed` or `event=job.failed`

## Console Output

The console is a live status surface; `desktop.jsonl` remains the full source of truth. In the
default `compact` mode, structured events are projected into short lines such as:

```text
2026-07-02T10:19:33.456Z [info] photo_editor.perf: preview.render.completed stage=interactive mode=live_preview reason=slider_drag elapsed=142ms queue=9ms budget=OVER/100ms backend=metal dirty=layers_composite cache=2/6 gen=33
```

High-cardinality details such as file paths, state signatures, async job ids, full dirty-stage
arrays, and byte counters stay in JSONL. Use `PHOTO_EDITOR_OBSERVABILITY_CONSOLE=json` when you
need the previous full-message terminal stream. Use `over_budget` when you only want terminal
attention for slow renders, failures, cancellations, and warnings. Repeated Qt warnings are still
written to JSONL every time, but the terminal prints only milestone repeats so image-format noise
does not bury performance events.

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
scripts/perf/run-benchmark.py \
  --scenario perf-preview-raster \
  --app build/dev-macos-debug/tests/preview_benchmark_driver \
  --corpus perf-corpora/preview-synthetic-v1/manifest.json \
  --event benchmark.preview_edit_sweep.completed \
  -- --corpus perf-corpora/preview-synthetic-v1/manifest.json --limit 1 --edit-sweep
scripts/perf/run-benchmark.py \
  --scenario perf-preview-raster \
  --app build/dev-macos-debug/tests/preview_benchmark_driver \
  --corpus perf-corpora/preview-synthetic-v1/manifest.json \
  --event benchmark.preview_global_burst.completed \
  -- --corpus perf-corpora/preview-synthetic-v1/manifest.json --limit 1 --global-burst
scripts/perf/run-benchmark.py \
  --scenario perf-preview-raster \
  --app build/dev-macos-debug/tests/preview_benchmark_driver \
  --corpus perf-corpora/preview-synthetic-v1/manifest.json \
  --event benchmark.preview_full_cache_repeat.completed \
  -- --corpus perf-corpora/preview-synthetic-v1/manifest.json --limit 1 --repeat-full-cache
scripts/perf/run-benchmark.py \
  --scenario perf-preview-raster \
  --app build/dev-macos-debug/tests/preview_benchmark_driver \
  --corpus perf-corpora/preview-synthetic-v1/manifest.json \
  --event benchmark.preview_output_profile_toggle.completed \
  -- --corpus perf-corpora/preview-synthetic-v1/manifest.json --limit 1 --output-profile-toggle
scripts/perf/run-benchmark.py \
  --scenario perf-preview-raster \
  --app build/dev-macos-debug/tests/preview_benchmark_driver \
  --corpus perf-corpora/preview-synthetic-v1/manifest.json \
  --event benchmark.preview_rapid_supersede.completed \
  -- --corpus perf-corpora/preview-synthetic-v1/manifest.json --limit 10 --rapid-supersede --timeout-ms 20000
scripts/perf/run-benchmark.py \
  --scenario perf-preview-raster \
  --app build/dev-macos-debug/tests/preview_benchmark_driver \
  --corpus perf-corpora/preview-synthetic-v1/manifest.json \
  --event benchmark.preview_brush_tile_cache.completed \
  --timeout 60 \
  -- --corpus perf-corpora/preview-synthetic-v1/manifest.json --limit 1 --brush-tile-cache --timeout-ms 30000
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
Passing `--edit-sweep` applies a deterministic adjustment sequence through the same controller
setters used by the UI and emits `benchmark.preview_edit_sweep.completed`; use this to prove
`preview.intent.received`, render coalescing, render modes, and publish-path telemetry are present.
Passing `--global-burst` ignores corpus recipes, applies a clean exposure/contrast burst, and emits
`benchmark.preview_global_burst.completed`; use this to prove exact draft promotion removes the
otherwise duplicate settled preview render for simple global edits.
Passing `--repeat-full-cache` retries preview rendering for the same asset and recipe in the same isolated runtime and
emits `benchmark.preview_full_cache_repeat.completed`; use this to prove repeated max-preview work
publishes from the disk preview cache instead of decoding, adjusting, and re-encoding. A retry with
a fresh cache hit should emit `preview.render.disk_cache_direct_scheduled`, proving the scheduler
skipped redundant settled-preview work and directly published cached max-preview output.
Passing `--output-profile-toggle` warms a visible adjusted preview, toggles the output profile, and
emits `benchmark.preview_output_profile_toggle.completed`; use this to prove display-transform-only
changes reuse the pre-display adjusted working-image cache instead of rerunning the adjustment
pipeline for visible and max-preview publication.
Passing `--rapid-supersede` starts full-resolution max-preview work, commits a newer edit while that
work is active, and fails if no obsolete render is cancelled. It emits
`benchmark.preview_rapid_supersede.completed`; use this together with `preview.render.cancelled` to
prove latest-request-wins cancellation, cancellation latency, and that stale full-resolution work
does not encode or publish over the newer generation.
Passing `--brush-tile-cache` generates a deterministic 2048x2048 square source, seeds an
input-stage-safe max-preview tile cache with `base_exposure=0.2`, renders a localized brush mask
through the adjusted-tile cache path, renders a full-frame reference, and emits
`benchmark.preview_brush_tile_cache.completed`. The event records seed, optimized, and reference
timings plus adjusted-stage tile counts, output tile counts, base exposure, and the maximum 8-bit
channel delta between optimized and reference output.
`thumbnail_benchmark_driver` loads the same manifest through `CatalogAssetModel`, waits for a visible
window of cold thumbnails, then constructs a fresh warm-cache model and emits
`benchmark.thumbnail_corpus.completed` for both passes.

The script understands both current records with a top-level `event` and older desktop records with
`fields.event`. When `--compare` is provided, it prints p50 and p95 deltas for matching event timing
fields.

`preview.graph.resolved` records the dirty-domain and dirty-stage list selected for a render reason
or edit intent. Coalesced bursts keep the earliest dirty stage required by all pending edits, so a
late display-only operation does not make earlier tone/color work look display-only.
`preview.render.completed` records also include decoded-cache, linear-cache, adjusted-working-cache,
rendered-preview-cache, and disk-preview-cache lookup/hit state, cache entry and byte totals, the decode backend used for the source image, the render backend used
for adjustments, the GPU fallback reason when preview falls back to CPU, the explicit `render_mode`
and `render_priority`, the dirty domain and dirty stage list, the preview `publish_path`, whether
JPEG encode was requested, and GPU readback count. These fields are part of the max-resolution preview rendering contract: live and
settled visible previews should publish through memory/native texture paths, while max-preview or
disk-cache work is expected to be separately identifiable.
Rendered export `job.completed` and `job.failed` records include the same `backend` and
`fallback_reason` fields so export throughput can be grouped by CPU versus Metal eligibility.
Histogram and scope computation now runs as a separate preview-side background task and emits
`preview.histogram.completed` with `elapsed_ms`, sample count, clipping counts, `render_reason`, and
`interaction_mode`. Histogram activity is intentionally separate from the main preview `processing`
state, so a published preview can become interactive while scopes finish or get superseded in the
background.
Diagnostic focus, clipping, false-color, gamut, and zone overlays are also noncritical preview-side
work. When max-preview work is pending or active, overlay generation is delayed until preview
rendering is idle; tests should observe these through diagnostic overlay sources or image-provider
keys, not through the main preview `processing` state.
Rapid repeated preview requests may emit `preview.render.coalesced`, including the number of
follow-up requests folded into the pending interactive render.
When a draft live preview is exact enough to satisfy the settled visible-preview contract,
`preview.render.draft_promoted` records the skipped duplicate settled render before max-preview work
continues.
When a fresh disk preview cache can satisfy pending max-preview work,
`preview.render.disk_cache_fast_scheduled` records that the full-preview debounce was bypassed behind
visible preview work; `preview.render.disk_cache_direct_scheduled` records that a retry skipped
settled-preview work and directly queued cached max-preview publication.
When a pre-display adjusted working-image cache can satisfy pending max-preview work,
`preview.render.adjusted_cache_fast_scheduled` records that the normal full-preview debounce was
bypassed and the cached max-preview render was queued directly behind visible preview work.
For adjusted working-image cache keys, visible-preview renders whose scaled size is already the
source size are canonicalized to the same source-resolution key as max-preview renders. This lets
`preview.render.completed` prove full-fit visible-preview to max-preview reuse with
`backend=adjusted_cache`, `adjusted_cache_hit=true`, and `max_edge=0` on the max-preview record.
When a final rendered-preview cache can satisfy pending no-adjustment max-preview work,
`preview.render.rendered_cache_fast_scheduled` records that the normal full-preview debounce was
bypassed and the cached max-preview render was queued directly behind visible preview work. This is
for base/no-adjustment sources where the visible preview already rendered at full source size; the
following max-preview completion should record `backend=rendered_cache`, `rendered_cache_hit=true`,
`decode_ms=0`, `edit_ms=0`, and `max_edge=0`.
Converted edit controls emit `preview.intent.received` with `operation`, `phase`, `dirty_domain`,
`render_reason`, `schedule_full_resolution`, and `approximate_ok`. Local edits should also include
normalized dirty-region fields once those controls are migrated to typed intents.
Preview stage dispatch emits `preview.render.stage_queued` with `queue_wait_ms`, and completed
preview records include the same queue-wait value.
Stale preview render completions emit `preview.render.cancelled` with the cancelled stage,
generation, latest generation, pending replacement state, cancellation count, queue wait, decode,
edit, encode, and total elapsed time. A correct rapid-supersede run should show the cancelled
generation lower than `latest_generation`, and obsolete full-resolution work should normally record
`encode_ms=0`.
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
