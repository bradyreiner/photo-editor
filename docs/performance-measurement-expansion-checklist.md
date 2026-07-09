# Performance Measurement Expansion Checklist

This checklist tracks the work needed to make performance, memory, worker usage,
render cost, tool latency, and UI responsiveness measurable across the app. Mark
items only when the implementation and completion gate are both satisfied by
current repo evidence.

## Goal 1: Unified Metric Contract

- [x] Keep structured JSONL as the canonical event stream.
- [x] Document the shared field vocabulary for workflow, stage, elapsed time,
  queue wait, cache, backend, fallback, and thread fields.
- [x] Define stable `workflow_id`, `interaction_id`, `tool_id`, and `surface`
  fields for all app workflows.
- [x] Standardize memory/resource fields: `rss_bytes`, `rss_delta_bytes`,
  `cache_bytes`, `gpu_memory_bytes`, `active_worker_count`, and
  `pending_worker_count`.
- [x] Update `scripts/perf/summarize-jsonl.py` so reports can group metrics by
  workflow, tool, stage, backend, and surface.

Completion gate:

- [x] `scripts/perf/summarize-jsonl.py --all-events` can group metrics by
  workflow, tool, stage, backend, and surface.
- [x] Existing events still parse without breaking backward compatibility.

## Goal 2: Resource Sampling

- [x] Sample process peak RSS for benchmarked app processes.
- [x] Capture process RSS at workflow start and end.
- [x] Capture peak RSS per workflow.
- [x] Capture RSS delta per workflow.
- [x] Capture decoded image cache bytes.
- [x] Capture linear/adjusted/rendered preview cache bytes.
- [x] Capture thumbnail cache entries and bytes.
- [x] Capture native texture bytes where available.
- [x] Capture GPU memory estimates where available.
- [x] Emit `workflow.resource_sampled`.
- [x] Emit `workflow.resource_summary`.

Completion gate:

- [x] Every benchmark artifact reports `rss_start`, `rss_peak`, `rss_end`, and
  `rss_delta`.
- [x] App-suite report has a memory/resource table.
- [x] A repeated workflow can detect unbounded memory growth.

## Goal 3: Render Stage Timing

- [x] Emit top-level preview render timing with queue, decode, edit, encode, and
  total elapsed fields.
- [x] Emit texture upload timing and upload pixel counts.
- [x] Emit per-stage render timing through `render.stage.completed`.
- [x] Measure `source_decode`.
- [x] Measure `raw_decode`.
- [x] Measure `input_color_transform`.
- [x] Measure `crop_geometry`.
- [x] Measure `mask_rasterization`.
- [x] Measure `layer_composite`.
- [x] Measure `global_tone_color`.
- [x] Measure `detail_spatial`.
- [x] Measure `retouch_patch`.
- [x] Measure `display_transform`.
- [x] Measure `overlay_composite`.
- [x] Measure `histogram_scope`.
- [x] Measure `encode_cache`.
- [x] Measure `native_texture_upload`.
- [x] Measure `image_provider_publish`.

Completion gate:

- [x] `preview.render.completed` includes total latency plus a full stage
  breakdown.
- [x] App-suite report shows render-stage p50/p95/max.
- [x] Any slow render can be traced to a specific stage.

## Goal 4: Tool Interaction Metrics

- [x] Define stable `tool_id` values for every major tool.
- [x] Add interaction telemetry for global sliders.
- [x] Add interaction telemetry for color tools.
- [x] Add interaction telemetry for tone curves.
- [x] Add interaction telemetry for crop/geometry.
- [x] Add interaction telemetry for layers.
- [x] Add interaction telemetry for retouch.
- [x] Add interaction telemetry for compare/view toggles.
- [x] Emit `tool.interaction.started`.
- [x] Emit `tool.stage.completed`.
- [x] Emit `tool.interaction.completed`.

Completion gate:

- [x] Every major tool reports first feedback latency, draft preview latency,
  settled preview latency, and full-resolution restore latency.
- [x] App-suite report has a tool latency table.
- [x] Tool interactions can be compared before and after optimization.

## Goal 5: Mask Performance Metrics

- [x] Emit structured mask work telemetry.
- [x] Record mask job cancellation and stale-result telemetry.
- [x] Measure live brush preview elapsed time.
- [x] Measure brush input-to-overlay latency as a standard event.
- [x] Measure brush point count.
- [x] Measure brush raster target size.
- [x] Measure dirty region size.
- [x] Measure mask tile cache hit/miss as a standard event.
- [x] Measure smart selection worker elapsed time as a standard event.
- [x] Measure refine edge worker elapsed time as a standard event.
- [x] Measure inpaint worker elapsed time as a standard event.
- [x] Measure full-resolution matte rebuild time.
- [x] Measure preview matte reuse versus rebuild.
- [x] Emit `mask.brush.feedback_completed`.
- [x] Emit `mask.raster.completed`.
- [x] Emit `mask.tile_cache.sampled`.
- [x] Emit `mask.smart_select.completed`.
- [x] Emit `mask.refine_edge.completed`.
- [x] Emit `mask.inpaint.completed`.

Completion gate:

- [x] Mask brush feedback p95 is visible in every relevant artifact.
- [x] Mask cache hit rate is visible.
- [x] Smart selection/refine/inpaint worker timing is visible.
- [x] Mask workflows show worker pressure, memory delta, and render impact.

## Goal 6: Worker And Queue Registry

- [x] Track thumbnail active worker count and queue wait.
- [x] Track preview render queue wait, pending/running state, and cancellation.
- [x] Track export/job queue wait and worker elapsed time.
- [x] Add a central worker registry.
- [x] Register preview render workers.
- [x] Register overlay render workers.
- [x] Register histogram/scope workers.
- [x] Register thumbnail workers.
- [x] Register mask commit workers.
- [x] Register smart selection workers.
- [x] Register inpaint workers.
- [x] Register export workers.
- [x] Register catalog import workers.
- [x] Register metadata extraction workers.
- [x] Register timeline media workers.
- [x] Register carousel render workers.
- [x] Emit `worker.queue.sampled`.
- [x] Emit `worker.queue.summary`.

Completion gate:

- [x] Every benchmark artifact reports active workers, pending workers, queue
  wait, completed count, and cancelled count by pool.
- [x] App-suite report shows worker pressure by workflow.
- [x] Worker starvation is visible without manual debugging.

## Goal 7: QML And UI Frame Metrics

- [x] Measure actual QML workflow stage readiness in the app workflow driver.
- [x] Add route timing for all major workspaces as standard events.
- [x] Measure Library route readiness.
- [x] Measure Import route readiness.
- [x] Measure Edit route readiness.
- [x] Measure Masks page readiness.
- [x] Measure Export route readiness.
- [x] Measure Timeline route readiness.
- [x] Measure Carousel route readiness.
- [x] Add frame interval sampling.
- [x] Add jank/dropped-frame counting.
- [x] Add Loader/component creation timing.
- [x] Emit `qml.route.started`.
- [x] Emit `qml.route.ready`.
- [x] Emit `qml.frame.sampled`.
- [x] Emit `qml.frame.summary`.

Completion gate:

- [x] App-suite report shows route p95 and frame/jank counts.
- [x] Workspace switching regressions are measurable.
- [x] UI freezes can be tied to render, worker, memory, or QML frame pressure.

## Goal 8: Benchmark Driver Expansion

- [x] Include startup, preview raster, thumbnail cold/warm, preview pipeline,
  color adjustment, interaction workflow, actual app workflow, preview frontier,
  mask performance, and Metal proof suites in the app-suite wrapper.
- [x] Add `perf-edit-global-sliders`.
- [x] Add `perf-edit-color-tools`.
- [x] Add `perf-edit-tone-curves`.
- [x] Add `perf-mask-brush`.
- [x] Add `perf-mask-lasso`.
- [x] Add `perf-mask-smart-select`.
- [x] Add `perf-layer-adjustments`.
- [x] Add `perf-retouch`.
- [x] Add `perf-crop-geometry`.
- [x] Add `perf-export-batch`.
- [x] Add `perf-import-large-folder`.
- [x] Add `perf-library-scroll`.
- [x] Add `perf-timeline-edit`.
- [x] Add `perf-carousel-preview`.
- [x] Add `perf-workspace-switch`.

Completion gate:

- [x] Each benchmark emits raw JSONL events and a summary event.
- [x] Each benchmark can run through `run-benchmark.py`.
- [x] Each benchmark can be included in `run-app-performance-suite.py`.

## Goal 9: App-Suite Reporting

- [x] Add suite status, warning/error, attention event, and timing tables.
- [x] Add workflow latency table.
- [x] Add tool latency table.
- [x] Add render-stage table.
- [x] Add memory growth table.
- [x] Add cache pressure table.
- [x] Add worker pressure table.
- [x] Add QML frame/jank table.
- [x] Add cancellation/wasted-work table.
- [x] Add backend/fallback table.
- [x] Write machine-readable `metrics.json`.

Completion gate:

- [x] One app-suite artifact clearly shows bottlenecks by workflow, tool, stage,
  memory, workers, and backend.
- [x] Reports support before/after comparison.
- [x] Optimization claims can be backed by artifact paths and metric deltas.

## Goal 10: Performance Gates

- [x] Provide smoke, local-dev, and release-proof suite profiles.
- [x] Add smoke thresholds for all core metrics.
- [x] Add local-dev thresholds for all core metrics.
- [x] Add release-proof thresholds for all core metrics.
- [x] Add optional stress thresholds.
- [x] Gate preview p95.
- [x] Gate brush feedback p95.
- [x] Gate thumbnail queue wait.
- [x] Gate workspace route readiness.
- [x] Gate render cancellation latency.
- [x] Gate memory growth.
- [x] Gate worker queue pressure.
- [x] Gate QML jank.

Completion gate:

- [x] CI can run fast smoke gates.
- [x] Local-dev gates catch common regressions.
- [x] Release-proof gates require representative workload artifacts.
- [x] Stress gates can be run manually before major releases.

## Goal 11: Baseline And Comparison Workflow

- [x] Store accepted artifact paths in `docs/performance-baselines.md`.
- [x] Add baseline comparison to app-suite reports.
- [x] Show p50/p95/max deltas.
- [x] Show memory delta changes.
- [x] Show worker queue changes.
- [x] Show cache hit-rate changes.
- [x] Show backend/fallback changes.

Completion gate:

- [x] Every optimization PR can include metric, before, after, workload,
  artifact path, and risk.
- [x] Regressions are visible as numeric deltas, not subjective impressions.

## Final Completion Gate

This work is complete when:

```sh
scripts/perf/run-app-performance-suite.py --profile local-dev
```

produces one artifact that answers, for every major surface/tool:

- [x] p50/p95/max latency
- [x] render stage breakdown
- [x] memory delta and peak RSS
- [x] cache hit/miss and cache bytes
- [x] worker queue depth and active worker count
- [x] cancellation/supersession count
- [x] backend and fallback reason
- [x] QML route time and frame/jank impact
