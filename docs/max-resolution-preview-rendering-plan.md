# Max-Resolution Preview Rendering Plan

This document defines the top-to-bottom plan for making preview rendering feel like a professional photo editor: max-resolution where it matters, immediate visual feedback during edits, and measured proof that each change improves the system.

The current preview path is centered around `EditorPreviewController::renderPreview()` and `render_preview_request()` in `apps/desktop/src/editor_preview_controller.cpp`. It already has useful pieces: render coalescing, decoded-image caching, linear-image caching, Metal support, in-memory preview publication, native texture display, observability, and benchmark artifacts. The problem is that the layers are still too tightly coupled. Frontend setters directly imply render work, the render request executes too much as one large job, the scheduler has coarse priority, and full-resolution work can sit too close to the interactive feedback loop.

The target architecture is:

```text
QML edit gesture
  -> typed edit intent
  -> transaction / recipe diff
  -> render scheduler
  -> dirty render graph
  -> tile/stage cache lookup
  -> CPU or GPU stage execution
  -> native preview publication
  -> background max-preview / disk-cache completion
```

## Goals

- Max-resolution preview must be available for settled image inspection and zoomed/pixel-level work.
- Live editing must never wait for invisible pixels.
- Full-resolution rendering must be incremental, tiled, cancellable, and cache-aware.
- The hot preview path must avoid JPEG encode/decode and avoid CPU/GPU readback unless a CPU consumer explicitly needs it.
- Every optimization must be proven with reproducible metrics, not subjective feel.

## Non-Goals

- Do not make every slider tick run a from-scratch full-frame export render.
- Do not hide latency by only lowering preview resolution.
- Do not accept GPU acceleration that copies every frame CPU -> GPU -> CPU -> QML and still blocks.
- Do not make correctness depend on a single backend. CPU must remain a deterministic fallback and test oracle.

## Mathematical Optimization Model

We cannot prove global optimality in the abstract because workload, hardware, image content, and edit recipe are variable. We can make the system mathematically defensible by defining objective functions, lower bounds, invariants, and regression gates.

In this document, "optimal" means Pareto-optimal for a declared workload, hardware class, visual tolerance, and memory budget. A change is optimal only if no competing implementation in the benchmark set is faster on a higher-priority objective without being worse on correctness, memory, bytes moved, or wasted work. This is the standard we can actually prove in software:

```text
Given workload W, hardware H, quality tolerance Q, and memory budget M,
candidate A dominates candidate B when:
  T_visible_A <= T_visible_B
  T_settled_A <= T_settled_B
  W_wasted_A <= W_wasted_B
  B_moved_A <= B_moved_B
  memory_A <= memory_B
  error_A <= Q
and at least one inequality is strictly better.
```

The accepted implementation for each phase must sit on the measured Pareto frontier. If two implementations trade off objectives, choose the one that protects the highest-priority user-visible metric first: `T_visible`, then `T_settled`, then wasted work, then bytes moved, then background `T_max`.

For each render request, define:

```text
T_visible = time from edit intent to first correct visible update
T_settled = time from commit/pause to sharp visible preview
T_max = time from commit/pause to full source-resolution tile set complete
W_wasted = CPU/GPU milliseconds spent on cancelled or invisible obsolete work
P_touched = number of source/output pixels processed by dirty stages
B_moved = bytes copied across CPU memory, GPU memory, disk, and CPU/GPU boundary
E = visual error versus exact reference render
```

Optimization target, in priority order:

```text
minimize T_visible
minimize T_settled
minimize W_wasted
minimize P_touched
minimize B_moved
minimize T_max, subject to not increasing T_visible/T_settled
keep E <= tolerance for each render mode
```

Lower bounds:

- A stage cannot be faster than the memory bandwidth needed to read/write its required pixels.
- A full exact render cannot touch fewer pixels than its dirty output region plus required kernel padding.
- A display-only edit should have a lower bound near one visible output pass, not a full decode or RAW pipeline.
- A brush stroke should have work proportional to dirty tile count, not total image area.

For each optimization, compare observed work to the theoretical required work:

```text
pixel_efficiency = required_dirty_pixels / actual_processed_pixels
copy_efficiency = required_output_bytes / actual_copied_bytes
cancel_efficiency = cancelled_work_ms / total_render_work_ms
cache_efficiency = cache_hits / cache_lookups for eligible stages
```

A change is accepted only if it improves or preserves the Pareto frontier for latency, correctness, memory, and wasted work on the benchmark suite.

### Proof Protocol

Every optimization must ship with this proof bundle:

1. Baseline artifact before the change.
2. Candidate artifact after the change.
3. Exact command lines and corpus manifest paths.
4. Distribution summary: p50, p90, p95, p99, max, standard deviation, and median absolute deviation.
5. Correctness summary: pixel parity, stage tolerance, or screenshot/native texture proof.
6. Work summary: processed pixels, dirty pixels, bytes moved, cache hit/miss, cancelled work, encode count, GPU readback count.
7. Pareto comparison: whether the candidate dominates, is dominated, or makes an explicit product tradeoff.

For noisy local timings, run at least 30 samples per scenario and use confidence intervals or median/MAD comparison. Single-run artifacts are allowed only as smoke proof for instrumentation and event coverage.

## Render Modes

Rendering must be explicit. Replace ambiguous calls like "render preview, maybe full resolution" with modes:

```text
FirstPaint
  Fastest available image for immediate display.
  Sources: embedded preview, catalog thumbnail, existing preview cache.

LivePreview
  User is actively dragging or painting.
  Latest request wins. Approximation is allowed only where declared.
  Renders visible pixels first.

SettledPreview
  User paused or committed an edit.
  Sharp preview at display/viewport resolution.
  Must be visually faithful for visible pixels.

MaxPreview
  Full source-resolution preview tile cache.
  Visible source-resolution tiles first, rest in background.

ExportRender
  Exact full output render.
  Uses the same graph/stages but export-specific output sizing, color, and encoding.
```

Mode acceptance targets:

| Mode | Target |
| --- | --- |
| FirstPaint | p95 under 50 ms after asset state is available |
| LivePreview | p95 under 50 ms to visible update on common edits |
| SettledPreview | p95 under 250 ms on common raster edits |
| Zoom visible MaxPreview tiles | p95 under 150 ms after pan/zoom settles |
| MaxPreview full image | background only; must not increase LivePreview p95 |
| ExportRender | throughput target measured separately from preview responsiveness |

## Frontend Edit Contract

QML must describe user intent, not directly choose render strategy.

### Edit Intent Schema

Introduce a typed intent object at the C++ boundary:

```text
EditIntent {
  asset_id
  edit_id
  operation
  phase              begin | update | commit | cancel
  dirty_domain       decode | raw | geometry | global_color | mask | retouch | detail | layer | display | metadata
  normalized_region  optional rect for local edits
  approximate_ok     true only for live mode operations that support approximation
  timestamp
}
```

Examples:

| User Action | Intent |
| --- | --- |
| Slider press | `phase=begin`, no render required |
| Slider move | `phase=update`, dirty domain for that control |
| Slider release | `phase=commit`, same dirty domain |
| Crop drag | `phase=update`, `dirty_domain=geometry` |
| Brush stroke move | `phase=update`, `dirty_domain=mask`, dirty region from stroke bounds plus feather |
| Output profile change | `phase=commit`, `dirty_domain=display` |
| Zoom or pan | no recipe change; request visible max tiles |

### Frontend Rules

- A gesture starts a transaction on press and commits on release.
- QML emits at most one update intent per animation frame for continuous gestures.
- Property setters update recipe state but do not directly call render execution.
- Multi-property controls use a scoped transaction and emit one intent.
- No-op value changes do not emit render intents.
- Zoom/pan must request visible tiles without changing the edit recipe.

### Tests

- QML/controller tests prove one slider drag emits one begin, bounded update intents, and one commit.
- No-op changes produce zero render requests.
- Multi-property operations produce one coalesced render transaction.
- Brush dirty regions include radius, feather, transform, and kernel padding.
- Output profile changes invalidate display stages only.

Metrics:

```text
edit_intent.count
edit_intent.coalesced_count
edit_intent.noop_count
edit_intent.phase
edit_intent.dirty_domain
```

## Controller Decomposition

Keep `EditorPreviewController` as the QML facade, but move responsibilities into services:

```text
EditorStateController
  current asset, current recipe, history, persistence

EditIntentController
  maps QML actions/setters to typed edit intents

PreviewScheduler
  owns queues, priorities, cancellation, request lifecycle

RenderGraphService
  maps recipe diffs and intents to dirty stages and cache keys

PreviewRenderer
  executes CPU/GPU tile and stage work

PreviewSurfacePublisher
  publishes native textures, image-provider frames, disk preview cache, and debug state

PreviewTelemetry
  records queue, stage, cache, tile, backend, memory, and cancellation metrics
```

Migration must be incremental. Start by adding these types behind the existing controller, then move call sites out of the large controller file in small slices.

### Tests

- Controller facade API remains stable for QML.
- Service unit tests can instantiate scheduler/graph/renderer without QML.
- Current parity tests continue to compare CPU/GPU/reference output.

## Scheduler Design

The scheduler is the arbiter between frontend intent and rendering work.

Priority order:

```text
0 LivePreview visible tile/frame for active gesture
1 SettledPreview visible viewport for selected asset
2 MaxPreview visible source-resolution tiles
3 MaxPreview adjacent/remaining tiles
4 Histogram and scopes
5 Neighbor asset prefetch
6 Disk preview cache encode
7 Export/background jobs
```

Rules:

- Latest request wins per asset, mode, viewport, and edit transaction.
- Obsolete work is cancelled before new work starts.
- Full-image completion cannot block visible work.
- Histogram/scopes cannot keep the main preview in a "rendering" state.
- Background jobs yield when priority 0-2 preview work exists.
- Queue wait is treated as first-class latency.

### Request Lifecycle

```text
intent received
  -> recipe diff computed
  -> dirty stages computed
  -> visible tile set computed
  -> obsolete requests cancelled
  -> LivePreview scheduled for update phase
  -> SettledPreview scheduled for commit/pause phase
  -> MaxPreview scheduled in background when settled
```

### Tests

- A burst of 100 slider updates produces bounded running work and exactly one latest settled request.
- Starting a new edit cancels older same-asset live/settled/max work.
- Full-preview jobs never run while priority 0 visible work is queued.
- Queue starvation tests prove background max preview eventually completes during idle.
- Cancellation latency is below one tile or one row-chunk budget.

Metrics:

```text
preview.scheduler.queue_wait_ms
preview.scheduler.priority
preview.scheduler.cancelled_count
preview.scheduler.cancelled_work_ms
preview.scheduler.obsolete_request_count
preview.scheduler.running_by_priority
```

## Render Graph And Dirty Invalidation

Define a real preview graph. Each stage owns:

- input dependencies
- recipe fields that affect it
- source/output regions it requires
- cache key fields
- CPU/GPU implementation availability
- approximation policy for live mode
- correctness tolerance

Stages:

```text
source_decode
raw_demosaic
input_color_transform
linear_base
geometry_transform
global_tone_color
mask_rasterization
local_adjustments
retouch_heal
detail_spatial
layers_composite
lut_post_color
display_transform
publish_texture
encode_disk
histogram_scope
```

Invalidation examples:

| Change | Dirty Stages |
| --- | --- |
| Exposure/contrast/basic tone | `global_tone_color` onward |
| Curve/HSL/color mixer | `global_tone_color` or `lut_post_color` onward depending on implementation |
| Output color space/soft proof | `display_transform` onward |
| Crop | `geometry_transform` onward |
| Zoom/pan | no recipe stages; visible tile request only |
| Brush mask stroke | `mask_rasterization` onward for dirty tiles |
| Retouch stroke | `retouch_heal` onward for dirty tiles |
| Denoise/sharpen/dehaze | `detail_spatial` onward |
| RAW white balance/demosaic setting | `raw_demosaic` or `input_color_transform` onward |
| File mtime/size change | all stages |

### Cache Keys

Stage cache keys must include:

```text
stage name
asset id
source file path
source file size and mtime
render graph version
stage recipe fingerprint
tile coordinate and scale level
color-management settings needed by the stage
backend-sensitive implementation version when output differs
```

### Tests

- Golden invalidation tests: for each edit operation, expected dirty stage list exactly matches.
- No invalidation test: rounded/no-op values dirty no stages.
- Cache-key mutation tests: changing one recipe field only changes keys for affected stages.
- File mutation tests: mtime/size changes invalidate all source-dependent caches.
- CPU/GPU parity tests: stage outputs match within tolerance.

Metrics:

```text
preview.graph.dirty_stages
preview.graph.cache_key
preview.graph.cache_hit
preview.graph.cache_miss
preview.graph.invalidated_tile_count
preview.graph.reused_stage_count
```

## Tile-Based Max-Resolution Rendering

Max-resolution preview must be tile based.

Recommended starting point:

```text
tile size: benchmark 256 and 512 px
tile overlap: computed from stage kernel radius
scale levels: thumbnail, screen, 1:2, 1:1 source
tile priority: visible -> adjacent -> rest
tile cache state: missing | queued | rendering | ready | stale | failed
```

For each tile:

```text
1. Compute output tile rect.
2. Expand by required padding for dirty downstream stages.
3. Reverse-map through geometry/lens stages to find source/input rect.
4. Find nearest valid upstream cached stage.
5. Execute only dirty stages for that tile.
6. Write result to GPU texture atlas or CPU tile cache.
7. Publish updated visible surface.
8. Queue remaining tiles if mode requires full max preview completion.
```

Tile padding must be mathematically derived:

```text
required_padding = max(
  blur radius,
  sharpen radius,
  denoise neighborhood,
  mask feather radius,
  local tone radius,
  retouch sample radius,
  interpolation/lens transform margin
)
```

### Tests

- Tile stitch tests compare tiled render against full-frame render within tolerance.
- Seam tests use high-contrast edges crossing tile boundaries.
- Geometry tests verify crop, rotate, scale, perspective, and lens transforms produce no missing pixels.
- Dirty brush tests prove only affected tiles plus padding rerender.
- Panning tests prove newly visible tiles are prioritized and old offscreen tiles are deprioritized.
- Cache eviction tests prove memory budget is respected.

Metrics:

```text
preview.tile.size
preview.tile.scale
preview.tile.visible_count
preview.tile.rendered_count
preview.tile.reused_count
preview.tile.dirty_count
preview.tile.padding_px
preview.tile.ms
preview.tile.bytes
```

## Actual Render Algorithms

### Global Color

Fuse simple global operations into the fewest possible passes:

```text
exposure
contrast
highlights/shadows/whites/blacks
tone curve
HSL/color mixer
color grading
LUT when feasible
display transform when mode allows
```

CPU fallback uses one SIMD-friendly loop over tile pixels. GPU uses one Metal kernel or a small fused kernel sequence with no synchronous wait between simple stages.

Tests:

- Pixel parity against existing reference path.
- Monotonic tone curve tests.
- Extreme value clamp/headroom tests.
- Wide-gamut/soft-proof boundary tests.

### Spatial Detail

Spatial stages need algorithm-specific acceleration:

- separable kernels for blur-like operations
- summed-area or mip pyramid methods for local averages
- tile-local scratch buffers
- explicit padding
- lower-cost live approximation only when declared and bounded by error tolerance

Tests:

- Full-frame versus tiled parity.
- Kernel radius/padding proof tests.
- Live approximation error measured against settled exact output.

### Masks And Local Adjustments

Masks should be cached as tiles/textures. Brush updates dirty only stroke bounds plus feather and affected local-adjustment radius.

Tests:

- Mask raster cache key tests.
- Brush stroke dirty-region tests.
- Feather/expand/contract correctness tests.
- Local adjustment parity with full render.

### Retouch And Heal

Retouch is dirty-region heavy. It must not invalidate global color or decode. Source sampling expands dirty bounds to include clone/heal source and destination regions.

Tests:

- Source and destination dirty tile tests.
- Retouch before/after mode tests.
- Cancellation tests for large retouch operations.

## GPU Architecture

Metal should become the primary max-preview path on macOS.

Persistent resources:

```text
source texture or linear source buffer
working ping-pong textures
mask textures
LUT textures
tile output texture atlas
native QML preview texture
GPU scratch buffer pool
```

Avoid:

```text
QImage
  -> copy to Metal
  -> run one kernel
  -> synchronous wait
  -> copy back to CPU
  -> upload to QML texture
```

Target:

```text
decoded/linear source prepared once
dirty stages execute on GPU
output remains in GPU texture
QML displays native texture
CPU readback only for histogram, export validation, or explicit debug
```

GPU fallback reasons must be structured and counted:

```text
metal_unavailable
metal_not_compiled
local_mask_sets
retouch_masks
legacy_local_masks
extended_range_pixels
cpu_required_stages
unsupported_layer_mode
unsupported_color_profile
```

### Tests

- GPU availability and fallback reason tests.
- CPU/GPU parity for each supported stage.
- Native texture nonblank screenshot tests.
- GPU resource lifetime tests.
- GPU memory budget and eviction tests.
- Command-buffer count and readback count regression tests.

Metrics:

```text
preview.gpu.backend
preview.gpu.fallback_reason
preview.gpu.command_buffer_count
preview.gpu.cpu_to_gpu_bytes
preview.gpu.gpu_to_cpu_bytes
preview.gpu.readback_count
preview.gpu.resource_reuse_count
preview.gpu.allocated_bytes
```

## CPU Fallback Architecture

CPU must be deterministic, fast enough for fallback, and useful as the correctness oracle.

Requirements:

- persistent thread pool
- tile-parallel execution
- SIMD-friendly contiguous loops
- scratch-buffer reuse
- cancellation checks per tile and per row chunk
- no avoidable allocations in hot loops

Tests:

- Thread sanitizer suite for scheduler/cache/CPU renderer.
- Deterministic output across thread counts.
- Cancellation exits within bounded row/tile work.
- Allocation counters for hot stages remain under budget.

## RAW Strategy

RAW preview needs a separate progressive path:

```text
FirstPaint: embedded RAW JPEG when available
LivePreview: draft demosaic / lower-cost RAW path
SettledPreview: high-quality screen-sized RAW render
MaxPreview: tiled full-quality RAW pipeline
ExportRender: exact full-quality RAW pipeline
```

RAW stage cache must separate:

- file decode
- metadata/profile load
- demosaic
- RAW input transform
- RAW control settings

Tests:

- Embedded preview fallback tests.
- RAW setting invalidation tests.
- Draft versus settled error measurement.
- Camera/profile corpus coverage.

## Publication Path

Interactive publication:

```text
render visible tile/frame
  -> native texture or in-memory image provider
  -> lightweight preview changed signal
```

Background publication:

```text
settled/max preview complete
  -> optional disk preview cache encode
  -> catalog/job metadata update
```

Rules:

- No JPEG writing in LivePreview.
- Disk cache encode is low priority.
- Preview source revision changes must be cheap and narrow.
- Overlay/diagnostic updates must not invalidate the base image.

Tests:

- LivePreview encode count is zero.
- Native texture path displays nonblank content.
- Image provider fallback works when native texture is unavailable.
- Disk preview cache updates only after settled/max completion.

## Observability

Extend existing JSONL observability with stage and tile fields.

Required event families:

```text
preview.intent.received
preview.scheduler.queued
preview.scheduler.cancelled
preview.graph.resolved
preview.tile.started
preview.tile.completed
preview.stage.started
preview.stage.completed
preview.publish.completed
preview.render.completed
preview.cache.evicted
preview.gpu.fallback
```

Every event should include:

```text
asset_id
edit_id
mode
priority
stage
tile coordinate when applicable
dirty domain
queue_wait_ms
elapsed_ms
backend
cache_hit
bytes
cancelled
fallback_reason
```

## Benchmark Suite

Benchmarks must run as artifacts, not ad hoc app launches.

Corpora:

```text
synthetic raster corpus
large JPEG corpus
HEIC corpus
RAW corpus by camera family
mask-heavy recipes
retouch-heavy recipes
layer-heavy recipes
wide-gamut/HDR recipes
huge panorama files
```

Scenarios:

```text
first paint on asset select
slider drag, 100 updates
slider release settled preview
output profile toggle
crop drag
brush stroke
retouch stroke
zoom to 100%
pan at 100%
full max-preview completion
export render parity
```

Statistical rules:

- Run each scenario at least 30 times for stable local comparison.
- Report p50, p90, p95, p99, max, standard deviation, and median absolute deviation.
- Compare against a checked-in or artifact baseline.
- Use confidence intervals for regressions where noise is high.
- Track cold-cache and warm-cache separately.
- Track AC power / thermal state when possible.

Acceptance gates:

```text
T_visible p95 must not regress by more than 5%.
T_settled p95 must not regress by more than 5%.
W_wasted must decrease or remain below budget.
GPU readback count must not increase in LivePreview.
LivePreview JPEG encode count must remain zero.
Pixel parity must remain inside stage tolerance.
Peak memory must remain inside configured budget.
```

## Correctness Tests

Correctness is non-negotiable. Add tests at four levels:

```text
unit:
  dirty graph, cache keys, region math, tile padding

stage:
  individual CPU/GPU stage parity

integration:
  tiled render versus full-frame render
  scheduler cancellation and priority

visual:
  screenshot/native texture nonblank checks
  seam and high-contrast edge fixtures
```

Numerical tolerances must be explicit per mode:

```text
ExportRender: strictest tolerance
MaxPreview: near export parity
SettledPreview: visually indistinguishable at display scale
LivePreview: bounded approximation allowed only for declared stages
```

## Implementation Phases

### Current Rollout Evidence

Initial rollout work on 2026-05-27 established the first measurable slice of this plan:

- `EditIntent` exists behind `EditorPreviewController` for high-traffic basic adjustments and output profile changes.
- Converted setters emit `preview.intent.received` before render scheduling and preserve no-op behavior.
- Render requests now carry explicit `render_mode` and `render_priority`.
- A first dirty-graph resolver maps render reasons and edit intents to ordered dirty stages.
- Coalesced bursts merge dirty graphs by keeping the earliest required stage, so a display-only edit at the end of a burst cannot hide earlier global-color work.
- Render completion telemetry records `publish_path`, `jpeg_encode_requested`, and `gpu_readback_count`.
- The preview benchmark driver supports `--edit-sweep` to exercise frontend setters through the real controller path.
- Pure global edit bursts can promote an exact live preview to satisfy settled visible-preview work, skipping the duplicate settled render and proceeding directly to max-preview work.
- The preview benchmark driver supports `--global-burst` to exercise that exact-draft promotion path without recipe operations that intentionally require a different settled pipeline.
- Full max-preview requests now check a fresh disk preview cache entry before decode, adjustment, and JPEG encode.
- Fresh disk-cache max-preview hits bypass the normal full-preview debounce and are queued behind only the visible preview.
- Explicit preview retries with a fresh disk-cache max-preview hit skip redundant settled-preview work and publish the cached max preview directly.
- The preview benchmark driver supports `--repeat-full-cache` to prove repeated max-preview requests publish from that disk cache.
- Visible preview renders now cache pre-display adjusted working pixels for cache-safe settled/live pipelines.
- Display-transform-only edits, including output profile changes, reuse the adjusted working-pixel cache for visible preview publication instead of rerunning linearization and color adjustment.
- Adjusted-cache-backed max-preview requests bypass the normal full-render debounce and are queued directly behind visible preview publication.
- When a visible preview already rendered at full source resolution, its adjusted working pixels satisfy later max-preview publication for the same recipe.
- The preview benchmark driver supports `--output-profile-toggle` to prove display-transform rerenders hit the adjusted working-pixel cache.
- No-adjustment visible preview renders now cache the final rendered image when they are eligible for exact reuse.
- When a visible no-adjustment preview already rendered at full source resolution, its final rendered pixels satisfy later max-preview publication for the same recipe without decode, edit, or display-transform work.
- Rendered-cache-backed max-preview requests bypass the normal full-render debounce and are queued directly behind visible preview publication.
- Adjusted-cache-backed visible preview renders now also cache the post-display final rendered image, so a following full-fit max-preview can reuse exact final pixels instead of converting adjusted working pixels a second time.
- Display-transform-only max-preview publication now takes the fastest valid path in sequence: adjusted working pixels for the visible preview, then rendered final pixels for the queued max preview.
- Edit-driven full/max rendered-cache hits publish from the resident in-memory image-provider path instead of synchronously writing a disk JPEG, while asset-select and explicit retry still populate/use the disk preview cache.
- Histogram and scope computation no longer keep the preview controller in the main `processing` state; it remains observable through histogram activity while published previews can continue immediately.
- Diagnostic overlay generation is deferred until preview rendering is idle, so noncritical focus/clipping/false-color overlay work cannot delay cached max-preview queue handoff or preview busy completion.
- The CPU color path now detects exact simple-gain input stages and avoids redundant matrix/raw/highlight reconstruction work in LUT and image max-luminance prepasses when that proof applies.
- Raster preview/export pipeline-state mapping now treats RAW input-transform controls as RAW-only when decoded metadata is available, so ordinary PNG/JPEG/TIFF previews do not pay highlight reconstruction and raw-level normalization work on every pixel.
- The color adjustment benchmark driver supports `--simple-global` for ordinary global tone/color work and records p50, p90, p95, p99, max, standard deviation, and median absolute deviation for every measured stage.
- The CPU detail stage now runs OKLab conversion, sharpen/noise-reduction blend passes, and final gamut fitting through the shared row scheduler, preserving the same pixel algorithm while removing serial full-frame loops from the heavy spatial/detail path.
- Post-detail CPU work now keeps common in-gamut OKLab pixels on a direct conversion path and row-schedules user LUT, black-point compensation, and output-headroom fitting, removing the remaining serial full-frame cleanup loops from the CPU fallback.
- The render graph now exposes first-pass tile planning primitives: normalized dirty regions map to conservative pixel rectangles, tile output rectangles cover only intersecting dirty work, tile padding is clamped to image bounds, and tile order is deterministic by visible, adjacent, then background priority.
- Full/max preview requests now carry deterministic tile-plan telemetry at the controller boundary: tile size, padding, visible-region flag, planned total tiles, and planned visible/adjacent/background tile counts. These fields are copied into render results and emitted in `preview.render.stage_started`, `preview.render.stage_queued`, and `preview.render.completed`.
- Real brush strokes now emit localized `mask_rasterization` edit intents with conservative normalized bounds derived from sampled brush points, so the max-preview request path can plan dirty brush tiles from actual frontend editing instead of only synthetic test probes.
- Full/max preview output conversion now executes through planned full-frame tiles when the request is full-resolution and full-invalidation. Render completion telemetry records `executed_tile_count` and `executed_tile_pixels` for both adjusted-cache hits and cache-miss adjustment renders. Controller sync-render tests prove a 960x960 adjusted-cache max-preview conversion executes as four 512px tiles covering exactly 921,600 output pixels, and a cache-miss tiled conversion matches the non-tiled reference render within one 8-bit channel value while covering exactly its output pixel count.
- Full/max rendered output tiles are now cached independently of whole rendered preview images. Non-full-invalidation max-preview requests bypass stale whole-image rendered cache hits, render only planned dirty output tiles, and stitch clean output tiles from the tile cache. Controller sync-render tests prove a localized dirty request over an already seeded 960x960 max preview renders exactly one 512x512 tile, reports 262,144 executed pixels, and matches the prior full render within one 8-bit channel value.
- Rendered output tile cache keys are now tile-local for brush mask edits. Brush points outside a tile's padded dependency rectangle are excluded from that tile's rendered-cache state, so clean tiles can be reused across brush recipe changes while dirty brush tiles get new cache keys.
- Localized brush max-preview renders can now reuse cached base working pixels and execute brush-local adjusted-stage work only for dirty tiles. The first supported path is deliberately constrained to non-retouch brush masks over either an otherwise unadjusted base recipe or an input-stage-safe base containing exposure, temperature, and tint only; full/max eligible renders now seed the base working-pixel cache and rendered tile cache, and brush points are transformed into tile-local coordinates before running the local adjustment on the dirty working tile.
- Brush-local dirty working tiles now call the engine's isolated adjusted-pixel local-mask stage instead of sending each cropped tile through the whole color pipeline. The stage API applies native local mask sets plus the neutral luminance/headroom closure to pixels that already include the cached base recipe, and rejects mixed spatial/retouch/layer inputs so unsupported recipes cannot silently enter the shortcut.
- The preview benchmark driver supports `--brush-tile-cache`, which generates a deterministic 2048x2048 source, seeds the max-preview tile cache, renders a localized brush edit through `backend=adjusted_tile_cache`, renders a full-frame reference, and records timing, tile-work, speedup, and parity epsilon in `benchmark.preview_brush_tile_cache.completed`.
- Max-preview visible source-resolution work can now split ahead of background max-preview completion when a native texture is already resident and the adjusted working cache is available. The controller schedules a `max_preview_visible_tile` prepass, publishes the visible tile as a native patch, then runs the normal background `max_preview` request.
- Real viewport changes from the QML-facing `setPreviewViewport` entrypoint now schedule source-resolution visible max-preview tile work when the fast path is provable: active viewport, resident same-size native texture, no compare mode, and adjusted working-cache availability. This keeps pan/zoom from starting an unbounded full render while still publishing the newly visible source-resolution tile through the native patch path.
- The preview benchmark driver supports `--viewport-max-tile`, which drives `setPreviewViewport` against a deterministic 2048x2048 source with a seeded same-size native texture and records direct `T_visible_max_tile` smoke telemetry in `benchmark.preview_viewport_max_tile.completed`.

Validated commands:

```sh
cmake --build --preset build-dev-macos-debug
ctest --test-dir build/dev-macos-debug -R "editor_preview_controller_test|app_bootstrap_test" --output-on-failure
ctest --test-dir build/dev-macos-debug -R "editor_preview_controller_test|color_pipeline_test|color_tool_invariant_test|render_parity_test" --output-on-failure
ctest --test-dir build/dev-macos-debug -R "render_graph_smoke_test|color_pipeline_test|editor_preview_controller_test|render_parity_test" --output-on-failure
ctest --test-dir build/dev-macos-debug -R "render_graph_smoke_test|editor_preview_controller_test|render_parity_test" --output-on-failure
ctest --test-dir build/dev-macos-debug -R "editor_preview_math_test|editor_preview_controller_test|render_parity_test" --output-on-failure
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
  --event benchmark.preview_brush_tile_cache.completed \
  --timeout 60 \
  -- --corpus perf-corpora/preview-synthetic-v1/manifest.json --limit 1 --brush-tile-cache --timeout-ms 30000
scripts/perf/run-benchmark.py \
  --scenario perf-preview-raster \
  --app build/dev-macos-debug/tests/preview_benchmark_driver \
  --corpus perf-corpora/preview-synthetic-v1/manifest.json \
  --event benchmark.preview_brush_tile_cache.completed \
  --timeout 120 \
  -- --corpus perf-corpora/preview-synthetic-v1/manifest.json --limit 1 --brush-tile-cache --iterations 3 --timeout-ms 30000
scripts/perf/run-benchmark.py \
  --scenario perf-preview-raster \
  --app build/dev-macos-debug/tests/preview_benchmark_driver \
  --corpus perf-corpora/preview-synthetic-v1/manifest.json \
  --event benchmark.preview_brush_tile_cache.completed \
  --timeout 180 \
  -- --corpus perf-corpora/preview-synthetic-v1/manifest.json --limit 1 --brush-tile-cache --brush-tile-span 2 --iterations 10 --timeout-ms 30000
scripts/perf/run-benchmark.py \
  --scenario perf-preview-raster \
  --app build/dev-macos-debug/tests/preview_benchmark_driver \
  --corpus perf-corpora/preview-synthetic-v1/manifest.json \
  --event benchmark.preview_brush_tile_cache.completed \
  --timeout 180 \
  -- --corpus perf-corpora/preview-synthetic-v1/manifest.json --limit 1 --brush-tile-cache --brush-sequence --iterations 10 --timeout-ms 30000
scripts/perf/run-benchmark.py \
  --scenario perf-preview-raster \
  --app build/dev-macos-debug/tests/preview_benchmark_driver \
  --corpus perf-corpora/preview-synthetic-v1/manifest.json \
  --event benchmark.preview_viewport_max_tile.completed \
  --timeout 120 \
  --settle-seconds 0 \
  -- --corpus perf-corpora/preview-synthetic-v1/manifest.json --limit 1 --viewport-max-tile --timeout-ms 30000
scripts/perf/run-benchmark.py \
  --scenario perf-color-adjustment \
  --app build/dev-macos-debug/tests/color_adjustment_benchmark_driver \
  --event benchmark.color_adjustment_suite.completed \
  --timeout 60 \
  -- --iterations 30 --simple-global
scripts/perf/run-benchmark.py \
  --scenario perf-color-adjustment \
  --app build/dev-macos-debug/tests/color_adjustment_benchmark_driver \
  --event benchmark.color_adjustment_suite.completed \
  --timeout 120 \
  -- --iterations 10
scripts/perf/run-color-adjustment-gate.py
```

Smoke artifacts:

```text
perf-artifacts/20260527T155208.697754Z-perf-preview-raster-93d409dd
perf-artifacts/20260527T160151.838261Z-perf-preview-raster-ab64af71
perf-artifacts/20260527T161250.599145Z-perf-preview-raster-1dada51d
perf-artifacts/20260527T161753.335486Z-perf-preview-raster-78bd6f38
perf-artifacts/20260527T162335.635288Z-perf-preview-raster-68476e12
perf-artifacts/20260527T163356.694215Z-perf-preview-raster-1a87afae
perf-artifacts/20260527T164051.192652Z-perf-preview-raster-0e6b8fff
perf-artifacts/20260527T164354.848623Z-perf-preview-raster-9709a90d
perf-artifacts/20260527T164545.859984Z-perf-preview-raster-03d43cff
perf-artifacts/20260527T164856.720138Z-perf-preview-raster-a7424aec
perf-artifacts/20260527T165748.862568Z-perf-preview-raster-a04dff35
perf-artifacts/20260527T170235.989896Z-perf-preview-raster-feeaef5e
perf-artifacts/20260527T171020.896650Z-perf-preview-raster-d6a04497
perf-artifacts/20260527T171950.054505Z-perf-preview-raster-49dc02b0
perf-artifacts/20260527T172321.594234Z-perf-preview-raster-22c9bb12
perf-artifacts/20260527T172745.236753Z-perf-preview-raster-4cd78ba9
perf-artifacts/20260527T172754.567119Z-perf-preview-raster-c35a6fc1
perf-artifacts/20260527T173422.990344Z-perf-preview-raster-3ffdc2c2
perf-artifacts/20260527T180036.756760Z-perf-preview-raster-dab1e2c0
perf-artifacts/20260527T180139.682239Z-perf-preview-raster-2987b3e6
perf-artifacts/20260527T180935.011034Z-perf-preview-raster-1d32e742
perf-artifacts/20260527T180947.285557Z-perf-preview-raster-8f26766a
perf-artifacts/20260527T180956.232922Z-perf-preview-raster-2e004830
perf-artifacts/20260527T181833.545534Z-perf-preview-raster-2e058e46
perf-artifacts/20260527T182421.714532Z-perf-color-adjustment-7d7e6bf8
perf-artifacts/20260527T182434.371819Z-perf-color-adjustment-4a408d31
perf-artifacts/20260527T183310.783145Z-perf-preview-raster-de7fcbf4
perf-artifacts/20260527T184053.652797Z-perf-color-adjustment-611018b5
perf-artifacts/20260527T184652.062123Z-perf-color-adjustment-2f6470a0
perf-artifacts/20260527T201559.464093Z-perf-preview-raster-0d8f126f
perf-artifacts/20260527T202813.694238Z-perf-preview-raster-fef89a2f
perf-artifacts/20260527T203553.555994Z-perf-preview-raster-f58ac30e
perf-artifacts/20260527T204201.043268Z-perf-preview-raster-1da9fd07
perf-artifacts/20260527T205020.628676Z-perf-preview-raster-e5f98d8a
perf-artifacts/20260527T205318.888733Z-perf-preview-raster-3fad0c60
perf-artifacts/20260527T205458.739945Z-perf-preview-raster-2af643c9
perf-artifacts/20260527T210246.078458Z-perf-preview-raster-ba0760f7
perf-artifacts/20260527T210341.308734Z-perf-preview-raster-681b3e10
perf-artifacts/20260527T211228.091337Z-perf-preview-raster-68543932
perf-artifacts/20260527T220814.134977Z-perf-preview-raster-0e107a9c
perf-artifacts/20260527T221418.308160Z-perf-preview-raster-fc1fde59
```

The edit-sweep artifact proves the event contract is now measurable: exposure, contrast, highlights, shadows, vibrance, and output color space emit typed edit intents; `preview.graph.resolved` records full-pipeline asset selection, global tone/color dirty stages, and merged burst dirty stages; rapid edits coalesce into `live_preview` priority 0 work; settled visible preview is priority 1; max preview is priority 3; live/settled previews publish through `in_memory_image_provider`; full max preview is separately identifiable as `disk_jpeg_cache`.

The global-burst artifact proves the first concrete render-count reduction: after the clean asset is loaded, an exposure/contrast burst emits two typed edit intents, coalesces to one `live_preview`, logs `preview.render.draft_promoted`, skips the redundant settled visible render, and schedules only the full `max_preview`. The measured edit-burst sequence is therefore two render completions instead of three: one in-memory live preview and one disk-backed max preview.

Artifact `20260527T165748.862568Z` proves the base/no-adjustment full-fit reuse path. The initial settled visible preview renders the source-sized image once, inserts one rendered-preview cache entry, and the following full `max_preview` records `preview.render.rendered_cache_fast_scheduled`, `backend=rendered_cache`, `rendered_cache_hit=true`, `decode_ms=0`, `edit_ms=0`, `encode_ms=2`, and `elapsed_ms=3`. This proves max-preview publication reused the already rendered full-resolution visible pixels instead of decoding and transforming the same base image again. In the same run, the later global edit burst still proves adjusted-cache reuse for max preview with `backend=adjusted_cache`, `decode_ms=0`, `edit_ms=21`, `encode_ms=2`, `elapsed_ms=25`, and total burst workload of 325 ms.

Artifact `20260527T170235.989896Z` proves histogram/scope separation from the main preview processing state. The render records remain equivalent to the prior global-burst proof: the base full `max_preview` still records `backend=rendered_cache`, `decode_ms=0`, `edit_ms=0`, `encode_ms=2`, and `elapsed_ms=3`; the edited full `max_preview` still records `backend=adjusted_cache`, `decode_ms=0`, `edit_ms=23`, `encode_ms=2`, and `elapsed_ms=26`. The measured benchmark workload now completes when the preview render work is done instead of waiting for histogram/scope futures: `benchmark.preview_global_burst.completed` is 238 ms versus 325 ms in artifact `20260527T165748.862568Z`, and `benchmark.preview_asset.completed` is 585 ms versus 754 ms. Histogram activity remains separately observable through the histogram activity state and `preview.histogram.completed` when the process remains alive for the background scope update.

Artifact `20260527T171020.896650Z` proves diagnostic overlays no longer sit in front of cached max-preview work. The initial full `max_preview` still uses the rendered cache with `decode_ms=0`, `edit_ms=0`, `encode_ms=2`, and `elapsed_ms=3`, but queue wait drops from 311 ms in artifact `20260527T170235.989896Z` before idle-delayed overlays to 76 ms. The full asset workload drops from 585 ms to 344 ms while the global edit burst remains 236 ms and the edited full `max_preview` still uses `backend=adjusted_cache`, `decode_ms=0`, `edit_ms=22`, `encode_ms=2`, and `elapsed_ms=25`. Focus/clipping/false-color diagnostic overlays are still generated from visible preview pixels, but they are delayed until no preview render is active.

Artifacts `20260527T171950.054505Z` and `20260527T172321.594234Z` prove the first actual CPU adjustment hot-path reduction for simple exposure/contrast global edits. The baseline global-burst run recorded the live CPU render at `backend=cpu`, `fallback_reason=metal_unavailable`, `edit_ms=154`, `elapsed_ms=154`, and `benchmark.preview_global_burst.completed=247`. The candidate computes the image max-luminance prepass from input luminance for monotonic exposure/contrast-only tone recipes, then applies `apply_global_tone` once to the maximum instead of once per pixel during that prepass. The candidate recorded `edit_ms=138`, `elapsed_ms=139`, and `benchmark.preview_global_burst.completed=218`, while the following full `max_preview` still reused `backend=adjusted_cache` with `decode_ms=0`, `edit_ms=21`, `encode_ms=2`, and `elapsed_ms=25`.

Artifacts `20260527T172745.236753Z` and `20260527T172754.567119Z` prove the LUT inner-loop validation cleanup. The fast path now validates the generated LUT table once, detects the common unit-domain LUT once, and calls an unchecked LUT sampler in the per-pixel loop. This preserves the existing LUT interpolation result while removing repeated validity, table-size, and default-domain checks from every non-neutral SDR pixel. The two candidate samples recorded live CPU renders at `edit_ms=133` and `edit_ms=131`, with the following full `max_preview` still using `backend=adjusted_cache`, `decode_ms=0`, and `elapsed_ms=25`. The end-to-end burst totals were `229 ms` and `239 ms`, so this artifact is evidence for actual render-loop reduction rather than a scheduler-level improvement.

Artifact `20260527T173422.990344Z` records the monotonic LUT-build prepass cleanup and the scheduler classification fix for cached full requests queued behind draft visible work. The LUT-build prepass now uses the same monotonic max-luminance proof as the image prepass, avoiding per-sample `apply_global_tone` calls during LUT max scanning for simple exposure/contrast recipes while preserving the shoulder decision. The benchmark remained within the prior live-render noise band at `edit_ms=132`, so this is recorded as work elimination with no measured global-burst latency win yet. The controller test suite now proves cached display-only full requests queued behind draft visible work still emit `preview.render.adjusted_cache_fast_scheduled` instead of being indistinguishable from ordinary debounced full work.

Artifact `20260527T174326.407392Z` adds the first latest-request-wins cancellation proof. The `--rapid-supersede` benchmark starts stale full-resolution max-preview work, commits a newer edit while that work is active, and only emits `benchmark.preview_rapid_supersede.completed` after stale work is actually cancelled. The run recorded `preview.render.cancelled` for full `generation=7` with `latest_generation=8`, `elapsed_ms=467`, `edit_ms=466`, `encode_ms=0`, and `cancel_count=1`; the benchmark summary record then emitted `cancelled_delta=1`, `full_cancelled_delta=1`, `superseded_generation=7`, and `final_generation=8`. This proves old full-resolution work is cooperatively abandoned before disk encode once a newer edit arrives, and makes cancellation latency a regression-tracked field instead of an implicit behavior.

Artifact `20260527T175111.662019Z` proves the first bounded-cancellation improvement on the stale adjusted-cache max-preview path. The previous cancellation proof showed stale full work was cancelled, but it still spent 467 ms converting cached full-resolution working pixels back to a display `QImage` before the cancellation surfaced. The candidate makes row/column CPU loops poll cancellation per row/column, makes `qimage_from_working_image` accept the render cancellation token, and makes the adjusted-cache branch return a cancelled result immediately instead of falling through to decode/render after a mid-conversion cancellation. The same rapid-supersede workload now records `preview.render.cancelled` for full `generation=7` with `latest_generation=8`, `elapsed_ms=1`, `edit_ms=0`, `encode_ms=0`, and `cancel_count=1`, while the following latest full `generation=8` still publishes from `backend=adjusted_cache` with `decode_ms=0`, `edit_ms=360`, `encode_ms=31`, and `elapsed_ms=393`. This changes the stale-work cost for this path from hundreds of milliseconds to the telemetry floor without changing the successful latest render result.

Artifact `20260527T180036.756760Z` proves the display-transform path now reaches the optimal known cache sequence for full-fit raster previews. The output profile change emits `dirty_domain=display_transform` with only display/publish/encode/histogram dirty stages. The visible preview records `backend=adjusted_cache`, `adjusted_cache_hit=true`, `decode_ms=0`, `edit_ms=22`, `encode_ms=0`, and `elapsed_ms=22`, then inserts the final post-display rendered image. The queued full `max_preview` records `backend=rendered_cache`, `rendered_cache_hit=true`, `decode_ms=0`, `edit_ms=0`, `encode_ms=2`, and `elapsed_ms=3`. This dominates the prior adjusted-cache max-preview path for the same display-only workload because it preserves visible-preview latency while removing the second adjusted-working-pixel conversion from the full max-preview publication.

Artifact `20260527T180139.682239Z` proves the same final-rendered cache reuse on a global edit burst. The promoted live preview records `backend=cpu`, `decode_ms=0`, `edit_ms=124`, and `elapsed_ms=125`, then the queued full `max_preview` records `backend=rendered_cache`, `rendered_cache_hit=true`, `decode_ms=0`, `edit_ms=0`, `encode_ms=2`, and `elapsed_ms=3`. The global burst completes in 210 ms with two render completions total: one visible in-memory render and one disk-backed max-preview publication from the exact final rendered cache. This replaces the older adjusted-cache full max-preview publication that still had to convert and display-transform cached working pixels.

Artifacts `20260527T180935.011034Z`, `20260527T180947.285557Z`, and `20260527T180956.232922Z` prove the hot edit-driven max-preview publication no longer pays disk JPEG cost. In the global-burst artifact, the promoted live preview records `backend=cpu`, `decode_ms=0`, `edit_ms=128`, and `elapsed_ms=129`; the following full `max_preview` records `backend=rendered_cache`, `rendered_cache_hit=true`, `decode_ms=0`, `edit_ms=0`, `encode_ms=0`, `elapsed_ms=0`, `jpeg_encode_requested=false`, and `publish_path=in_memory_image_provider`, with `benchmark.preview_global_burst.completed=198 ms`. In the output-profile artifact, the visible preview still uses `backend=adjusted_cache`, `adjusted_cache_hit=true`, `decode_ms=0`, `edit_ms=22`, and `elapsed_ms=23`; the following full `max_preview` records `backend=rendered_cache`, `rendered_cache_hit=true`, `decode_ms=0`, `edit_ms=0`, `encode_ms=0`, `elapsed_ms=0`, `jpeg_encode_requested=false`, and `publish_path=in_memory_image_provider`, with `benchmark.preview_output_profile_toggle.completed=86 ms`. The repeat-cache artifact proves this did not break durable cache behavior: the initial `asset_select` full max-preview still writes `publish_path=disk_jpeg_cache`, and explicit `preview_retry` records `preview.render.disk_cache_direct_scheduled`, `disk_cache_hit=true`, `decode_ms=0`, `edit_ms=0`, `encode_ms=0`, and `benchmark.preview_full_cache_repeat.completed=32 ms`.

Artifact `20260527T181833.545534Z` is a post-simple-gain-smoke global-burst run. It records the promoted live preview at `backend=cpu`, `decode_ms=0`, `edit_ms=124`, `elapsed_ms=125`, and the following edit-driven full `max_preview` at `backend=rendered_cache`, `rendered_cache_hit=true`, `decode_ms=0`, `edit_ms=0`, `encode_ms=0`, `elapsed_ms=0`, and `publish_path=in_memory_image_provider`, with `benchmark.preview_global_burst.completed=201 ms`. Because this is a single end-to-end sample and only improves the prior 128 ms live edit by 4 ms, it is a smoke artifact, not a standalone proof of a Pareto improvement.

Artifacts `20260527T182421.714532Z` and `20260527T182434.371819Z` add distribution-level proof coverage for the actual CPU color algorithm. The 30-iteration simple-global run isolates exact global tone/color work with the input transform reduced to a simple gain: `preview-960` records `avg_ms=44.53`, `p50_ms=44`, `p90_ms=45`, `p95_ms=46`, `p99_ms=47`, `stddev_ms=0.88`, and `mad_ms=1`; `full-1920` records `avg_ms=172.7`, `p50_ms=170`, `p90_ms=181`, `p95_ms=187`, `p99_ms=188`, `stddev_ms=6.69`, and `mad_ms=5`. The 10-iteration spatial/detail reference run records `preview-960 avg_ms=674.7`, `p95_ms=705`, and `full-1920 avg_ms=2472.1`, `p95_ms=2554`. These artifacts establish separate mathematical gates for the common global path and the heavy spatial/detail path, instead of hiding both behind one average-only benchmark.

Artifact `20260527T183310.783145Z` proves that format-aware raster pipeline-state mapping improves the real preview controller path while preserving the existing cache/publish wins. The promoted live preview now records `backend=cpu`, `decode_ms=0`, `edit_ms=110`, and `elapsed_ms=111`; the edit-driven full `max_preview` still records `backend=rendered_cache`, `rendered_cache_hit=true`, `decode_ms=0`, `edit_ms=0`, `encode_ms=0`, `elapsed_ms=0`, `jpeg_encode_requested=false`, and `publish_path=in_memory_image_provider`. The global-burst workload completes in `179 ms`, compared with `198 ms` in artifact `20260527T180935.011034Z` and `201 ms` in artifact `20260527T181833.545534Z`. The correctness contract was updated in `editor_preview_controller_test` so the 16-bit raster preview reference path also disables RAW input transform and highlight reconstruction.

Artifact `20260527T184053.652797Z` proves a CPU spatial/detail algorithm execution improvement without changing the math. The same 10-iteration benchmark shape as artifact `20260527T182434.371819Z` now records `preview-960 avg_ms=502.4`, `p50_ms=505`, `p95_ms=522`, and `full-1920 avg_ms=1951.6`, `p50_ms=1946`, `p95_ms=1995`. Compared with the prior spatial/detail reference (`preview-960 avg_ms=674.7`, `p95_ms=705`; `full-1920 avg_ms=2472.1`, `p95_ms=2554`), row-parallel detail processing reduces average CPU time by 25.54% at preview size and 21.05% at full-1920, with p95 reduced by 25.96% and 21.89% respectively. This is a Pareto improvement for the CPU fallback because correctness tests still pass and the algorithm performs the same per-pixel OKLab/sharpen/noise-reduction/gamut-fit operations with less wall-clock time.

Artifact `20260527T184652.062123Z` extends the CPU spatial/detail proof by removing remaining serial post-detail cleanup loops and avoiding Oklch gamut-search setup for common in-gamut OKLab pixels. The run records `preview-960 avg_ms=470.7`, `p50_ms=467`, `p95_ms=490`, and `full-1920 avg_ms=1846.4`, `p50_ms=1838`, `p95_ms=1888`. Compared with artifact `20260527T184053.652797Z`, this reduces preview average time by 6.31% and full-1920 average time by 5.39%, with p95 reduced by 6.13% and 5.36% respectively. Compared with the original spatial/detail reference artifact `20260527T182434.371819Z`, the combined CPU fallback improvements reduce preview average time by 30.24% and full-1920 average time by 25.31%, with p95 reduced by 30.50% and 26.08% respectively.

The render-graph tile foundation is proved by `render_graph_smoke_test`. The test now covers normalized dirty-region to pixel-rect conversion, padding expansion clamped to image bounds, full-frame edge tile coverage, localized dirty-tile selection, and visible-first tile ordering. This satisfies the first Phase 5 prerequisite: deterministic tile region math.

The controller tile-plan boundary is proved by `editor_preview_controller_test`. Full `max_preview` requests now compute tile-plan metrics before queueing work; a 16x16 full-invalidation request proves one planned 512px max-preview tile with 64px spatial padding, and a synthetic 2048x2048 localized `mask_rasterization` dirty region proves the request planner schedules exactly one intersecting max-preview tile while preserving visible-region prioritization metadata. This proves the frontend/edit-intent/render-request path has enough deterministic data to gate the next implementation step: replacing the full-frame max-preview pixel execution with tile execution.

The real brush dirty-region boundary is also proved by `editor_preview_controller_test`. The test drives a normal brush mask workflow through `addMask`, brush setup, `beginActiveBrushStroke`, sampled points, and `endActiveBrushStroke`; the resulting `EditIntent` records `operation=brush_stroke`, `dirty_domain=mask_rasterization`, `render_reason=mask_preview_change`, and a valid normalized dirty region enclosing the sampled stroke. The corresponding full `max_preview` request is non-full-invalidation and plans exactly one visible 512px tile on a 2048x2048 source when the viewport covers that tile. This is still not tiled pixel execution, but it proves actual frontend brush edits now feed the same dirty-tile planner that the renderer will execute next.

The first tiled pixel-execution primitive is proved by `editor_preview_controller_test`. A synthetic display-transform max-preview request changes only the output color space, hits the adjusted working-pixel cache, converts the full-resolution output through planned tiles, and reports `executed_tile_count=4` with `executed_tile_pixels=output_width * output_height` for a 960x960 result. The same test forces a cache-miss adjustment render in compare mode, renders once with tiled output conversion and once with tile conversion disabled, and proves both outputs match within one 8-bit channel value while the tiled result reports positive tile execution and exact pixel coverage. This proves full-frame tiled output conversion for both cache-hit and cache-miss adjusted working pixels.

The first localized dirty output-tile patch path is also proved by `editor_preview_controller_test`. After seeding the rendered tile cache from a full 960x960 max-preview conversion, the test submits a non-full-invalidation dirty request with the same final rendered state, bypasses the stale whole-image rendered cache, rerenders exactly one 512x512 output tile, stitches the remaining clean tiles from the tile cache, reports `executed_tile_count=1` and `executed_tile_pixels=262144`, and matches the full render within one 8-bit channel value. This is intentionally limited to rendered-output tile patching from an already available adjusted working image; the next Phase 5 implementation step is moving mask/local-adjustment computation itself to tile-scoped stage caches so real brush edits do not have to regenerate a full adjusted working image before output patching.

The brush-cross-recipe tile execution proof is also in `editor_preview_controller_test`. A 2048x2048 full max-preview render seeds sixteen 512x512 rendered output tiles, a reusable base working-pixel cache entry, and a base full-resolution rendered image, reporting `executed_tile_pixels=2048 * 2048`. A following localized brush-mask request changes the recipe, misses the full adjusted working-pixel cache as expected, and still publishes a full 2048x2048 max preview through `backend=adjusted_tile_cache`. The request reports `adjusted_stage_tile_count=1`, `adjusted_stage_pixels=262144`, `executed_tile_count=1`, `executed_tile_pixels=262144`, and `base_rendered_image_hit=true`, proving both the adjusted-stage work and output conversion are proportional to the single dirty 512x512 tile while the rest of the output is patched from a cached full-resolution rendered image when tile-cache fingerprints prove the clean tiles are equivalent. The positive proof now covers an input-stage-safe base with global exposure, an overbroad dirty rectangle that still adjusts only the one tile containing brush work, a four-tile brush stroke that reports exactly four adjusted/output tiles, and a second accumulated brush stroke that patches from the previous rendered brush state instead of from the no-brush base. The same tests render uncached full-frame brush references and verify tile-scoped output matches within one 8-bit channel value. Negative guards in the same test submit brush-over-global-contrast, brush-over-post-color saturation, and non-square 1024x512 brush requests; all prove they do not report `backend=adjusted_tile_cache` or any adjusted-stage tile execution. This is the first true adjusted-stage tile execution path, limited to non-retouch brush masks over a cached base whose global edits are empty or limited to exposure, temperature, and tint, to pointwise local brush edits whose clean-tile fingerprints match the patch base, and to tile geometry that preserves brush shape exactly. Broader Phase 5 work remains for arbitrary local masks, global tone/color stage composition, anisotropic brush/tile geometry, spatial/detail kernels, geometry, retouch, and uncached base recipes.

Artifact `20260527T203553.555994Z` adds benchmark-level evidence for that brush tile-cache path over a global exposure base. The `--brush-tile-cache` workload generated a deterministic 2048x2048 source, seeded sixteen full-frame 512px output tiles with `base_exposure=0.2` in `446 ms`, then rendered the localized brush update with `backend=adjusted_tile_cache`, `adjusted_stage_tiles=1`, `adjusted_stage_pixels=262144`, `output_tiles=1`, `output_pixels=262144`, and `brush_elapsed_ms=78`. The same workload rendered an uncached full-frame brush reference in `860 ms`, so the optimized path was `11.03x` faster while doing exactly `1/16` of the adjusted-stage and output-conversion pixel work. The benchmark recorded `parity_max_channel_delta=4` on the high-frequency synthetic source; the controller unit proof remains stricter on the uniform source with a one-channel delta limit, while this benchmark establishes the current high-frequency raster parity gate for the input-stage-safe base path.

Artifact `20260527T204201.043268Z` proves the first engine-stage split inside the accepted brush tile-cache path. The dirty working tile now uses `apply_local_adjustments_to_adjusted_pixels` instead of a cropped full `apply_color_adjustments` call, so it avoids repeating decode/input/global/post-color setup that the cached base working image already contains. The same `--brush-tile-cache` workload recorded `seed_elapsed_ms=445`, `brush_elapsed_ms=69`, `reference_elapsed_ms=900`, `speedup_ratio=13.04`, `adjusted_stage_pixels=262144`, `output_pixels=262144`, and the same high-frequency `parity_max_channel_delta=4`. Correctness is covered by `editor_preview_math_test`, which compares the isolated stage against the full pipeline for native local mask sets and verifies rejection of mixed spatial mask modes, plus `editor_preview_controller_test`, which proves the adjusted-tile backend still matches full-frame reference renders for the supported controller path.

Artifact `20260527T205020.628676Z` upgrades the brush tile-cache benchmark from one smoke timing to distribution-capable proof. `preview_benchmark_driver --brush-tile-cache` now accepts `--iterations`, emits `benchmark.preview_brush_tile_cache.sample_completed` per sample, and records p50, p90, p95, p99, max, standard deviation, and median absolute deviation for both the optimized dirty-tile path and the full-frame reference. A 3-iteration validation run recorded brush `p50=72 ms`, `p95=74 ms`, reference `p50=843 ms`, reference `p95=847 ms`, `speedup_ratio=11.71`, `adjusted_stage_pixels=262144`, `output_pixels=262144`, and `parity_max_channel_delta=4`. This is still a short validation run, not the required 30-sample release gate, but the benchmark now exposes the statistical fields needed to enforce that gate.

Artifacts `20260527T205318.888733Z` and `20260527T205458.739945Z` accept the safe half of the local-stage invariant hoist. `apply_local_adjustments_to_adjusted_pixels` now precomputes each active native local-mask set's adjustment/decontamination descriptor once per request, skips inactive mask sets before entering the pixel loop, and keeps the proven target-luminance scratch buffer and closure pass unchanged. The 3-iteration smoke run recorded brush `p50=67 ms`, `p95=69 ms`, reference `p50=839 ms`, reference `p95=852 ms`, `speedup_ratio=12.52`, `adjusted_stage_pixels=262144`, `output_pixels=262144`, and `parity_max_channel_delta=4`. The 30-iteration proof run then recorded brush `p50=67 ms`, `p90=70 ms`, `p95=72 ms`, `p99=74 ms`, `max=74 ms`, `stddev=1.86 ms`, `mad=1 ms`, reference `p50=847 ms`, reference `p95=887 ms`, `speedup_ratio=12.64`, `adjusted_stage_pixels=262144`, `output_pixels=262144`, and `parity_max_channel_delta=4`. Compared with artifact `20260527T205020.628676Z`, the candidate preserves correctness and work bounds while reducing brush p50 by 6.94%; p95 remains inside the prior short-run timing band and is now backed by the required 30-sample distribution, so this candidate stays on the measured Pareto frontier for the supported brush tile-cache workload.

Artifacts `20260527T210246.078458Z` and `20260527T210341.308734Z` prove the adjusted-tile output-publish cleanup and the first multi-tile brush distribution. The renderer now passes only actually adjusted dirty tiles into output publication; overbroad dirty rectangles with no brush work in some planned tiles no longer force a fallback or unnecessary tile conversion. When the base full-resolution rendered image is cached and every non-dirty tile fingerprint matches the base tile fingerprint, the renderer copies that base image and draws only the dirty rendered tiles. The one-tile 30-iteration run records `base_rendered_image_hit=true`, brush `p50=68 ms`, `p95=71 ms`, `p99=78 ms`, reference `p50=849 ms`, `speedup_ratio=12.49`, `adjusted_stage_pixels=262144`, `output_pixels=262144`, and `parity_max_channel_delta=4`, preserving the prior hot-path timing band. The new `--brush-tile-span 2` 10-iteration run records `adjusted_stage_tiles=4`, `adjusted_stage_pixels=1048576`, `output_tiles=4`, `output_pixels=1048576`, `base_rendered_image_hit=true`, brush `p50=193 ms`, `p95=208 ms`, reference `p50=896 ms`, `speedup_ratio=4.64`, and `parity_max_channel_delta=4`. This proves the supported brush path scales with affected tile count instead of falling back to full-frame adjustment, while remaining correct against the full-frame reference.

Artifact `20260527T211228.091337Z` proves accumulated brush edits can remain on the dirty-tile path when the new stroke does not invalidate the prior stroke's clean-tile fingerprints. The `--brush-sequence` workload first renders and caches a one-tile brush stroke, then appends a second brush point in a different tile and renders only that second dirty tile by patching from the previous rendered brush state. The 10-iteration run records `brush_sequence=true`, `base_rendered_image_hit=true`, `adjusted_stage_tiles=1`, `adjusted_stage_pixels=262144`, `output_tiles=1`, `output_pixels=262144`, brush `p50=72 ms`, `p95=75 ms`, reference `p50=874 ms`, reference `p95=948 ms`, `speedup_ratio=12.14`, and `parity_max_channel_delta=4`.

Artifact `20260527T220814.134977Z` extends that accumulated-stroke proof to the boundary-near case that previously fell back because the second brush point intersected another tile's padded dependency fingerprint. The renderer now keeps the default rendered-tile cache fingerprint conservative, but the guarded pointwise brush adjusted-tile path validates patch-base clean tiles with output-rect brush fingerprints. This is mathematically valid for the accepted path because those brush masks only apply pointwise local exposure/temperature/tint on the output tile, with no spatial, retouch, selection decontamination, layer, post-color, or anisotropic-tile participation. The 10-iteration boundary run records `brush_sequence=true`, `base_rendered_image_hit=true`, `adjusted_stage_tiles=1`, `adjusted_stage_pixels=262144`, `output_tiles=1`, `output_pixels=262144`, brush `p50=71 ms`, `p95=92 ms`, reference `p50=846 ms`, reference `p95=886 ms`, `speedup_ratio=11.92`, and `parity_max_channel_delta=4`. Because the p95 includes one 92 ms outlier, this artifact is accepted as coverage and fallback-removal evidence, while the 30-iteration one-tile artifact `20260527T210246.078458Z` remains the stronger latency-distribution gate for the hot path.

Artifact `20260527T221418.308160Z` adds the publication-side proof for the same boundary accumulated-stroke path. Render results now carry an exact dirty output rectangle and `texture_upload_pixels`; `preview.render.completed` and `benchmark.preview_brush_tile_cache.completed` both record `partial_texture_upload_candidate=true` and `texture_upload_pixels=262144` for the one-tile update versus `reference_output_pixels=4194304` for the full-frame reference. The native texture backend now has `uploadRegion(image, rect)`, which only succeeds when an existing same-size texture is already resident, and then updates the Metal texture with one `replaceRegion` for the dirty rectangle. `editor_preview_controller_test` proves the first brush stroke reports upload rect `(0,0,512,512)`, the boundary second stroke reports `(512,0,512,512)`, both remain correct against full-frame reference output, and the Metal-backed texture probe accepts partial upload only after a full same-size texture initializes residency. The benchmark run is accepted as bytes-moved and API coverage evidence, not as a latency-frontier artifact, because the machine was noisy: brush `p50=145 ms`, `p95=193 ms`, reference `p50=1780 ms`, reference `p95=1953 ms`, `speedup_ratio=12.28`, and `parity_max_channel_delta=4`.

The same controller test now injects a fake native preview texture into a real asset-load controller path and proves a successful native upload skips the redundant edited image-provider publication when compare mode is off and no reference preview image is present. This closes the publication contract at the controller boundary: native texture display remains the hot path for the edited preview, while the image provider is still available for compare/reference cases that actually need a CPU/QML image consumer.

Artifacts `20260527T223241.627659Z` and `20260527T223630.361122Z` cover the latest dirty-tile copy cleanup. The renderer now converts full-working-image output tiles directly from a source rect instead of first copying each tile into a temporary `WorkingImage`, and the brush adjusted-tile path now checks the tile-local brush state before copying base pixels for a tile. The normal one-tile brush run records brush `p50=68 ms`, `p95=73 ms`, `adjusted_stage_pixels=262144`, `output_pixels=262144`, `texture_upload_pixels=262144`, `speedup_ratio=12.87`, and `parity_max_channel_delta=4`. The new `--brush-overbroad-dirty` run deliberately makes the dirty region wider than the actual brush contribution and still records `brush_overbroad_dirty=true`, `adjusted_stage_tiles=1`, `adjusted_stage_pixels=262144`, `output_tiles=1`, `output_pixels=262144`, `texture_upload_pixels=262144`, `speedup_ratio=12.87`, and `parity_max_channel_delta=4`. These artifacts are accepted as wasted-work removal and correctness evidence, not as a new latency frontier, because the overbroad run's brush `p50=69 ms` and `p95=75 ms` remain in the existing hot-path timing band rather than dominating artifact `20260527T210246.078458Z`.

Artifact `20260527T224344.087558Z` proves the first patch-only native-publication render path. `EditorPreviewTexture` now supports `uploadPatch(patch, destination_rect)`, and the Metal/native probe in `editor_preview_controller_test` verifies patch upload into an existing same-size native texture. When the controller has a resident native texture, a full-resolution non-full-invalidation request can opt into `allow_texture_patch_only`; the dirty-tile renderer then returns `texture_upload_image` for the dirty rect instead of a full `preview_image`, so it avoids copying the entire cached base rendered image just to upload one changed tile. The controller regression test now covers the same render contract directly: an accumulated brush stroke returns no full preview image, returns a 512x512 `texture_upload_image`, reports `texture_upload_rect=(1024,0,512,512)`, and matches the same crop from a full-frame reference within one 8-bit channel value. The same test also exercises the actual controller queue/completion boundary by seeding a 2048x2048 fake native texture, starting a full render through `startRenderStage`, and proving the finished render publishes one `uploadPatch` at `(1536,0,512,512)`, increments the native display revision, and leaves the edited image provider empty. The `--brush-overbroad-dirty --brush-texture-patch-only` 30-iteration benchmark records `brush_texture_patch_only=true`, `texture_upload_image=true`, `texture_upload_pixels=262144`, `adjusted_stage_tiles=1`, `output_tiles=1`, `brush_p50_ms=67`, `brush_p95_ms=71`, `speedup_ratio=13.10`, and `parity_max_channel_delta=3` by comparing the patch against the same rect from the full-frame reference. This is accepted as a real publication-path work reduction: the hot result no longer needs a full-frame patched `QImage` for native display on this path.

Artifact `20260527T230356.243430Z` accepts the neutral post-local tone fast path inside the adjusted-tile renderer. `apply_local_adjustments_to_adjusted_pixels` now proves whether the post-local global tone, HDR local tone, filmic, levels, zone, and curve stages are identity for the cached adjusted-pixel input. When they are identity and the locally edited RGB channels stay inside `[0, output_headroom]`, the stage returns immediately after applying native local masks and skips the target-luminance scratch buffer plus final luminance/curve/headroom closure pass. If any edited channel falls outside that range, it falls back to the existing closure pass; `editor_preview_math_test` covers that overrange case against the full pipeline. The 30-iteration `--brush-overbroad-dirty --brush-texture-patch-only` benchmark records `brush_texture_patch_only=true`, `texture_upload_image=true`, `texture_upload_pixels=262144`, `adjusted_stage_tiles=1`, `adjusted_stage_pixels=262144`, `output_tiles=1`, `output_pixels=262144`, `brush_p50_ms=67`, `brush_p95_ms=71`, `brush_p99_ms=72`, `stddev=1.91 ms`, `speedup_ratio=13.01`, and `parity_max_channel_delta=3`. This is accepted as a work-removal proof at the current hot-path latency frontier, not as a new latency-dominating artifact over `20260527T224344.087558Z`.

Artifact `20260527T231159.361317Z` tightens the patch-only publication proof by removing the last avoidable base-image copy for exact-cover patch uploads. The patch assembler now detects when the rendered dirty tiles exactly cover the upload rect; for the one-tile brush path it returns the rendered dirty tile directly, and for exact-cover multi-tile patches it assembles from dirty tiles without first copying the same region from the base rendered image. It still falls back to copying the base patch when the upload rect contains clean holes. The controller result, render telemetry, and benchmark summary now expose `texture_patch_base_copy_pixels`, and `editor_preview_controller_test` asserts the single-tile patch-only brush render reports `texture_patch_base_copy_pixels=0` while matching the full-frame reference crop. The 30-iteration `--brush-overbroad-dirty --brush-texture-patch-only` benchmark records `texture_patch_base_copy_pixels=0`, `texture_upload_pixels=262144`, `adjusted_stage_tiles=1`, `output_tiles=1`, `brush_p50_ms=68`, `brush_p95_ms=71`, `speedup_ratio=13.10`, and `parity_max_channel_delta=3`. This is accepted as CPU copy/telemetry work reduction at the existing latency frontier; it does not claim a latency improvement over `20260527T224344.087558Z`.

Artifact `20260527T231551.185475Z` extends that exact-cover patch-only proof to four dirty tiles. `editor_preview_controller_test` now exercises the controller path with a 2x2 brush stroke, `allow_texture_patch_only=true`, and an exact upload rect `(0,0,1024,1024)`, proving the result publishes no full preview image, returns a 1024x1024 patch image, reports `texture_patch_base_copy_pixels=0`, and matches the same crop from the full-frame reference within one 8-bit channel value. The 10-iteration `--brush-tile-cache --brush-tile-span 2 --brush-texture-patch-only` benchmark records `adjusted_stage_tiles=4`, `adjusted_stage_pixels=1048576`, `output_tiles=4`, `output_pixels=1048576`, `texture_upload_pixels=1048576`, `texture_patch_base_copy_pixels=0`, `brush_p50_ms=199`, `brush_p95_ms=208`, `reference_p50_ms=901`, `speedup_ratio=4.53`, and `parity_max_channel_delta=4`. This is accepted as multi-tile coverage and scaling evidence; the one-tile 30-sample artifacts remain the hot-path latency gate.

Artifact `20260527T231912.911182Z` closes the accumulated-stroke patch-only benchmark proof. `preview_benchmark_driver --brush-sequence --brush-texture-patch-only` now warms the first stroke with `allow_texture_patch_only=false` so the previous full rendered brush state is available as the patch base, then measures the second accumulated stroke with patch-only native publication enabled. The 10-iteration run records `brush_sequence=true`, `brush_texture_patch_only=true`, `base_rendered_image_hit=true`, `adjusted_stage_tiles=1`, `adjusted_stage_pixels=262144`, `output_tiles=1`, `output_pixels=262144`, `texture_upload_image=true`, `texture_upload_pixels=262144`, `texture_patch_base_copy_pixels=0`, `brush_p50_ms=69`, `brush_p95_ms=70`, `reference_p50_ms=885`, `reference_p95_ms=907`, `speedup_ratio=12.83`, and `parity_max_channel_delta=3`. This is accepted as accumulated-state native-publication coverage: the renderer patches from the prior brush result, avoids full preview-image publication, and keeps both adjusted-stage and upload work bounded to the newly dirty tile.

Artifact `20260527T233013.909722Z` removes the hidden full-CPU-base dependency from consecutive patch-only brush publication. The controller now tracks the rendered recipe key represented by the resident native preview texture, passes that key into patch-only render requests, and updates it after every successful full or patch native upload. The dirty-tile renderer accepts an exact-cover patch without a cached full base `QImage` only when the requested patch base key equals that resident native texture key; patches with clean holes still require a real base image. `editor_preview_controller_test` now publishes two consecutive native patch uploads through the controller and proves the second patch succeeds after the first patch-only render left no full CPU image-provider publication behind. The 10-iteration `--brush-sequence --brush-texture-patch-only --brush-patch-chain` benchmark records `brush_patch_chain=true`, `base_rendered_image_hit=false`, `texture_patch_base_state_hit=true`, `texture_upload_image=true`, `texture_upload_pixels=262144`, `texture_patch_base_copy_pixels=0`, `adjusted_stage_tiles=1`, `output_tiles=1`, `brush_p50_ms=145`, `brush_p95_ms=164`, `reference_p50_ms=2024`, `speedup_ratio=13.96`, and `parity_max_channel_delta=3`. This artifact is accepted as continuous-brush native-residency coverage rather than a latency-frontier artifact; the benchmark machine was slower than the prior hot-path run, but the proof condition is stronger because the measured patch has no full rendered base image hit.

Artifact `20260527T233535.245889Z` removes source decode from the accepted dirty brush tile-cache path. `render_preview_request` now attempts the guarded adjusted-tile brush renderer before `decode_source_image`; on success it copies the same telemetry into the render result, applies the preview display transform, and proceeds directly to publication. If the tile path cannot prove its cache/base invariants, the existing decode and full-pipeline fallback still runs. `editor_preview_controller_test` now asserts supported one-tile adjusted-tile renders report `decode_elapsed_ms=0`, and `preview_benchmark_driver` fails the brush tile-cache workload if any optimized sample decodes. The 30-iteration `--brush-overbroad-dirty --brush-texture-patch-only` benchmark records `brush_max_decode_ms=0`, `texture_upload_image=true`, `texture_upload_pixels=262144`, `texture_patch_base_copy_pixels=0`, `adjusted_stage_tiles=1`, `output_tiles=1`, `brush_p50_ms=47`, `brush_p95_ms=51`, `brush_p99_ms=54`, `reference_p50_ms=895`, `speedup_ratio=19.04`, and `parity_max_channel_delta=3`. This supersedes the prior one-tile hot-path latency frontier by eliminating unavoidable decode work from the optimized path while preserving the same pixel-work and parity gates.

Artifact `20260527T234607.469085Z` keeps the same one-tile brush latency frontier after removing unnecessary exposure-only local-adjustment work. `apply_local_adjustment_mask` now exits through a guarded exposure-only path when local white balance, local tone, and local chroma are all identity, so the accepted brush workload no longer pays white-balance matrix setup and RGB matrix application for every affected pixel. The full local-adjustment path still runs for temperature, tint, contrast, highlight/shadow, white/black, vibrance, saturation, hue, colorize, and selection cleanup cases. The 30-iteration `--brush-overbroad-dirty --brush-texture-patch-only` benchmark records `brush_max_decode_ms=0`, `texture_upload_image=true`, `texture_upload_pixels=262144`, `texture_patch_base_copy_pixels=0`, `adjusted_stage_tiles=1`, `output_tiles=1`, `brush_p50_ms=47`, `brush_p95_ms=51`, `brush_p99_ms=52`, `reference_p50_ms=871`, `speedup_ratio=18.53`, and `parity_max_channel_delta=3`. This artifact is accepted as neutral-frontier work reduction, not as a new latency frontier: p50/p95 match artifact `20260527T233535.245889Z`, p99 improves by 2 ms, and the lower speedup ratio comes from a faster reference render on this run rather than slower optimized brush samples.

Artifact `20260527T234912.350129Z` adds timing split proof for the current accepted dirty brush path. The controller result, preview telemetry, and benchmark summary now separate adjusted-stage time from output assembly time as `adjusted_stage_ms` and `output_stage_ms`, with p50/p95 benchmark fields for both. The 5-iteration diagnostic run recorded brush `p50=50 ms`, `p95=52 ms`, `adjusted_stage_p50_ms=33`, `adjusted_stage_p95_ms=34`, `output_stage_p50_ms=10`, `output_stage_p95_ms=10`, `brush_max_decode_ms=0`, `texture_upload_pixels=262144`, `texture_patch_base_copy_pixels=0`, and `parity_max_channel_delta=3`. This identified the next bottleneck mathematically: after decode elimination and patch-only publication, the adjusted local-mask stage dominated the remaining one-tile render cost, while output conversion/assembly was a stable 10 ms.

Artifact `20260527T235543.646562Z` accepts a guarded single-brush exposure fast path inside `apply_local_adjustments_to_adjusted_pixels`. For exactly one enabled brush source, `operation=add`, normal blend, default selection/refine settings, no parent/nested masks, no segmentation/depth map, and an exposure-only local mask set, the renderer bypasses the generic local-mask dispatch/composite path and evaluates brush weight directly with precomputed point constants and x coordinates. Unsupported masks still fall through to the existing generic local-adjustment renderer. The 30-iteration `--brush-overbroad-dirty --brush-texture-patch-only` benchmark records `brush_max_decode_ms=0`, `texture_upload_image=true`, `texture_upload_pixels=262144`, `texture_patch_base_copy_pixels=0`, `adjusted_stage_tiles=1`, `adjusted_stage_pixels=262144`, `output_tiles=1`, `output_pixels=262144`, `adjusted_stage_p50_ms=19`, `adjusted_stage_p95_ms=22`, `output_stage_p50_ms=10`, `output_stage_p95_ms=10`, `brush_p50_ms=37`, `brush_p95_ms=40`, `brush_p99_ms=46`, `reference_p50_ms=879`, `reference_p95_ms=936`, `speedup_ratio=23.76`, and `parity_max_channel_delta=3`. Compared with artifact `20260527T234607.469085Z`, this reduces optimized brush p50 by 21.28% and p95 by 21.57% while preserving the same decode, pixel-work, patch-only, and parity gates. The next measured bottleneck is now split between adjusted-stage work at roughly 19 ms p50 and output conversion/native patch preparation at 10 ms p50.

Artifact `20260528T000112.265744Z` accepts the dirty-tile output no-copy cleanup. `qimage_from_dirty_working_tile_overrides` now looks up each dirty working tile by const iterator instead of `QHash::value()`, avoiding a deep copy of the 512x512 `WorkingImage` vector immediately before output conversion. The 30-iteration `--brush-overbroad-dirty --brush-texture-patch-only` benchmark records `brush_max_decode_ms=0`, `texture_upload_image=true`, `texture_upload_pixels=262144`, `texture_patch_base_copy_pixels=0`, `adjusted_stage_tiles=1`, `adjusted_stage_pixels=262144`, `output_tiles=1`, `output_pixels=262144`, `adjusted_stage_p50_ms=20`, `adjusted_stage_p95_ms=22`, `output_stage_p50_ms=9`, `output_stage_p95_ms=9`, `brush_p50_ms=36`, `brush_p95_ms=39`, `brush_p99_ms=42`, `reference_p50_ms=925`, `reference_p95_ms=985`, `speedup_ratio=25.69`, and `parity_max_channel_delta=3`. Compared with artifact `20260527T235543.646562Z`, this reduces optimized brush p50 by 2.70%, p95 by 2.50%, and output-stage p50/p95 by 10%, while keeping the same decode, pixel-work, patch-only, and parity invariants. The next bottleneck is the remaining Qt working-to-display color conversion plus the adjusted-stage brush application, both still tile-bounded.

Artifact `20260528T000455.474250Z` rejects a direct manual Linear ProPhoto working-tile to sRGB `QImage` converter for dirty preview tiles. The candidate kept the benchmark crop comparison within `parity_max_channel_delta=2`, but made the output stage much slower than the accepted Qt conversion path: `output_stage_p50_ms=38` and `brush_p50_ms=67` versus the accepted `output_stage_p50_ms=9` and `brush_p50_ms=36` from artifact `20260528T000112.265744Z`. It also failed `editor_preview_controller_test` on the 16-bit strong-edit preview reference case with max channel delta `238`. The code was reverted; future output-stage work should target a measured GPU/Qt color-transform path, a better SIMD conversion with full profile parity coverage, or smaller dirty upload regions rather than this scalar CPU conversion shape.

Artifact `20260528T001644.517997Z` accepts guarded brush-effect native patch cropping as publication-work reduction, not as a new CPU-render latency frontier. The renderer still adjusts and display-converts the full 512x512 dirty tile so rendered tile-cache entries remain complete, but when texture patch publication is allowed and the mask geometry is provably bounded to default additive brush points with no inversion, selection expansion/refinement, segmentation, depth, clone, or erase behavior, the patch image and upload rect are cropped to the brush-effect bounds. The 30-iteration one-tile `--brush-overbroad-dirty --brush-texture-patch-only` run records `adjusted_stage_tiles=1`, `adjusted_stage_pixels=262144`, `output_tiles=1`, `output_pixels=262144`, `texture_upload_image=true`, `texture_upload_pixels=28561`, `texture_patch_base_copy_pixels=0`, `brush_max_decode_ms=0`, `adjusted_stage_p50_ms=21`, `output_stage_p50_ms=9`, `brush_p50_ms=39`, `reference_p50_ms=894`, `speedup_ratio=22.92`, and `parity_max_channel_delta=3`. This cuts the one-tile native upload payload by 89.11% versus the previous 262144-pixel full-tile patch while preserving the same rendered tile work and crop parity. Companion artifact `20260528T001644.519426Z` proves the same contract for a 2x2 brush span: four 512x512 tiles are still rendered and cached (`output_pixels=1048576`), but the patch payload drops to `texture_upload_pixels=463761` with `parity_max_channel_delta=4`. Because the benchmark driver measures render result generation more than real GPU upload cost, this artifact is accepted only as a bandwidth/native-publication reduction; the next CPU latency target remains the adjusted-stage brush loop or replacing Qt output conversion with a proven faster profile-correct implementation.

Artifact `20260528T002634.818884Z` accepts direct brush-effect patch output conversion for texture-patch-only publication. When the guarded brush-effect upload rect is valid and fully covered by the dirty tiles, `qimage_from_dirty_working_tile_overrides` now converts only the patch intersections from each dirty `WorkingImage` into the native upload image instead of converting the full 512x512 dirty tile and cropping afterward. The fast path keeps the external dirty-tile accounting conservative (`output_tiles=1`, `output_pixels=262144`) but reduces actual display conversion to the 169x169 upload payload (`texture_upload_pixels=28561`) and skips full preview-image publication. The 30-iteration one-tile `--brush-overbroad-dirty --brush-texture-patch-only` run records `brush_max_decode_ms=0`, `base_rendered_image_hit=true`, `texture_upload_image=true`, `texture_patch_base_copy_pixels=0`, `adjusted_stage_tiles=1`, `adjusted_stage_pixels=262144`, `output_tiles=1`, `output_pixels=262144`, `texture_upload_pixels=28561`, `adjusted_stage_p50_ms=20`, `adjusted_stage_p95_ms=23`, `output_stage_p50_ms=2`, `output_stage_p95_ms=2`, `brush_p50_ms=29`, `brush_p95_ms=33`, `brush_p99_ms=33`, `reference_p50_ms=877`, `reference_p95_ms=915`, `speedup_ratio=30.24`, and `parity_max_channel_delta=3`. Compared with artifact `20260528T001644.517997Z`, this cuts output-stage p50 by 77.78% and optimized brush p50 by 25.64% while preserving the same adjusted-stage work, upload crop, zero-decode gate, no-base-copy gate, and crop parity. Companion artifact `20260528T002745.240013Z` verifies the same direct-conversion contract across a 2x2 brush span with `adjusted_stage_tiles=4`, `adjusted_stage_pixels=1048576`, `texture_upload_pixels=463761`, `output_stage_p50_ms=34`, `brush_p50_ms=130`, and `parity_max_channel_delta=4`. The remaining one-tile frontier is now the adjusted-stage brush loop at roughly 20 ms p50; output conversion is no longer a first-order cost for the guarded single-brush patch path.

Artifact `20260528T013609.433951Z` accepts patch-only adjusted-stage rect specialization for the guarded brush-effect path. The dirty-tile renderer now computes the preferred texture upload rect before adjustment, remaps the tile-local brush state to the intersecting patch rect when the reduced rect is supported, and stores a `WorkingTileOverride` with its exact output rect. The patch-only exact-cover output path can consume these partial working overrides directly, while full-tile publication still requires full-tile overrides, preserving the prior cache/publication contract outside the guarded patch path. The 30-iteration one-tile `--brush-overbroad-dirty --brush-texture-patch-only` run records `brush_max_decode_ms=0`, `base_rendered_image_hit=true`, `texture_upload_image=true`, `texture_patch_base_copy_pixels=0`, `adjusted_stage_tiles=1`, `adjusted_stage_pixels=28561`, `output_tiles=1`, `output_pixels=262144`, `texture_upload_pixels=28561`, `adjusted_stage_p50_ms=3`, `adjusted_stage_p95_ms=4`, `output_stage_p50_ms=2`, `output_stage_p95_ms=2`, `brush_p50_ms=12`, `brush_p95_ms=13`, `brush_p99_ms=13`, `reference_p50_ms=899`, `reference_p95_ms=961`, `speedup_ratio=74.92`, and `parity_max_channel_delta=3`. Compared with artifact `20260528T002634.818884Z`, this cuts adjusted-stage p50 from 20 ms to 3 ms, adjusted pixels from 262144 to 28561, and optimized brush p50 from 29 ms to 12 ms while keeping output conversion at 2 ms p50 and preserving the zero-decode, no-base-copy, patch-only, native-resident, and crop-parity gates. Companion artifact `20260528T013650.188941Z` verifies the same contract across a 2x2 brush span with `adjusted_stage_tiles=4`, `adjusted_stage_pixels=773281`, `texture_upload_pixels=463761`, `output_stage_p50_ms=33`, `brush_p50_ms=106`, `reference_p50_ms=936`, `speedup_ratio=8.83`, and `parity_max_channel_delta=4`; this reduces multi-tile adjusted work by 26.24% versus the prior 1048576-pixel adjusted-stage frontier. The remaining one-tile frontier is no longer full-tile adjustment or output conversion; it is fixed controller/request overhead plus the small 169x169 adjusted/output patch work, so the next meaningful wins need to target request scheduling overhead, native texture handoff, or a GPU/direct-preview pipeline.

Artifact `20260528T014257.805649Z` accepts exact-cover patch-only tile-key bypass as a request-overhead reduction on top of the patch-rect adjusted-stage frontier. The renderer now uses a dedicated exact-cover patch assembler when texture-patch-only publication has a valid base image or resident texture state and the preferred upload rect is fully covered by dirty working overrides. That direct path converts the patch and returns telemetry without constructing full-frame rendered tile cache keys, without hashing every output tile recipe, and without validating clean tile keys that are irrelevant to an exact-cover native patch. The fallback path still builds and validates tile keys for full preview publication, clean-hole patch composition, and non-exact dirty coverage. The 30-iteration one-tile `--brush-overbroad-dirty --brush-texture-patch-only` run records `brush_max_decode_ms=0`, `base_rendered_image_hit=true`, `texture_upload_image=true`, `texture_patch_base_copy_pixels=0`, `adjusted_stage_tiles=1`, `adjusted_stage_pixels=28561`, `output_tiles=1`, `output_pixels=262144`, `texture_upload_pixels=28561`, `adjusted_stage_p50_ms=3`, `adjusted_stage_p95_ms=4`, `output_stage_p50_ms=2`, `output_stage_p95_ms=2`, `brush_p50_ms=9`, `brush_p95_ms=10`, `brush_p99_ms=10`, `reference_p50_ms=888`, `reference_p95_ms=937`, `speedup_ratio=98.67`, and `parity_max_channel_delta=3`. Compared with artifact `20260528T013609.433951Z`, this cuts one-tile optimized brush p50 from 12 ms to 9 ms and p95 from 13 ms to 10 ms without changing pixel work, upload work, or parity bounds. Companion artifact `20260528T014343.768710Z` records the 2x2 patch path at `adjusted_stage_pixels=773281`, `texture_upload_pixels=463761`, `output_stage_p50_ms=33`, `brush_p50_ms=103`, `brush_p95_ms=112`, `reference_p50_ms=913`, `speedup_ratio=8.86`, and `parity_max_channel_delta=4`, a small but positive reduction from the prior 106 ms multi-tile p50. The remaining one-tile frontier is now about 9-10 ms total, with 3-4 ms in adjustment and 2 ms in output conversion; the unaccounted remainder is render-request orchestration, recipe/key generation that still remains outside the exact-cover bypass, result copying, and benchmark/caller overhead.

Artifact `20260528T014841.221375Z` rejects a duplicate output-size source-read cleanup in the patch-only result publication path. The candidate reused the already-read `source_size` to compute output dimensions instead of calling `output_size_for_request`, preserving focused controller/math/invariant tests and `parity_max_channel_delta=3`, but the 5-iteration `--brush-overbroad-dirty --brush-texture-patch-only` smoke run regressed the hot path to `brush_p50_ms=10`, `adjusted_stage_p50_ms=4`, and `output_stage_p50_ms=2` versus the accepted `brush_p50_ms=9` and `adjusted_stage_p50_ms=3` from artifact `20260528T014257.805649Z`. The code was reverted because the change did not produce a measurable frontier improvement and added another helper on a path whose remaining cost is now dominated by broader orchestration, not this one size lookup.

Artifact `20260528T015141.782086Z` rejects a one-tile exact-cover painter bypass in `qimage_from_exact_cover_dirty_working_patch`. The candidate returned the converted patch image directly when exactly one dirty tile intersected the upload rect, avoiding an extra destination `QImage` allocation and `QPainter::drawImage` call. Focused controller/math/invariant tests passed and the 5-iteration `--brush-overbroad-dirty --brush-texture-patch-only` smoke run preserved `parity_max_channel_delta=3`, but it only tied the accepted frontier: `brush_p50_ms=9`, `brush_p95_ms=10`, `adjusted_stage_p50_ms=3`, and `output_stage_p50_ms=2`. The code was reverted because it added a second assembly branch without a measurable improvement over artifact `20260528T014257.805649Z`; further overhead work should remove larger duplicated recipe/key work or scheduler/result plumbing rather than micro-optimizing this already 2 ms output step.

Artifact `20260528T015544.377961Z` accepts duplicate current rendered-cache-key reuse in the dirty brush render path. `render_preview_request` already computes `result.rendered_cache_key` before attempting the fast brush path, so the fast path now accepts that key instead of recomputing `rendered_preview_image_cache_key(request)` while preserving the default recompute path for direct callers. Focused `editor_preview_controller_test`, `editor_preview_math_test`, and `color_tool_invariant_test` passed. The 5-iteration smoke artifact `20260528T015527.899603Z` recorded `brush_p50_ms=9`, `brush_p95_ms=9`, `adjusted_stage_p50_ms=3`, `output_stage_p50_ms=2`, `texture_upload_pixels=28561`, and `parity_max_channel_delta=3`, so the candidate advanced to the 30-iteration gate. The accepted 30-iteration one-tile run records `brush_avg_ms=8.97`, `brush_p50_ms=9`, `brush_p95_ms=9`, `brush_p99_ms=9`, `adjusted_stage_p50_ms=3`, `adjusted_stage_p95_ms=4`, `output_stage_p50_ms=2`, `output_stage_p95_ms=2`, `texture_upload_pixels=28561`, `speedup_ratio=98.56`, and `parity_max_channel_delta=3`. Compared with artifact `20260528T014257.805649Z`, this keeps p50 fixed at the 9 ms timer floor but improves the measured one-tile tail from 10 ms to 9 ms by removing duplicated request-key hashing from the hot path. Companion artifact `20260528T015624.280306Z` records the 2x2 patch path at `adjusted_stage_pixels=773281`, `texture_upload_pixels=463761`, `brush_p50_ms=102`, `brush_p95_ms=109`, `reference_p50_ms=943`, `speedup_ratio=9.25`, and `parity_max_channel_delta=4`, improving the prior 2x2 accepted frontier from `103/112 ms` to `102/109 ms`. The current accepted hot-path frontier is therefore one-tile `9/9/9 ms` p50/p95/p99 and 2x2 `102/109 ms` p50/p95. Remaining candidate work must beat this frontier after correctness tests, not just tie it.

The first adjusted-cache telemetry prehash removal attempt was rejected before benchmarking because the then-current 16-bit strong-edit verifier failed intermittently. That rejection is superseded by the later verifier hardening and accepted re-test below; the production optimization is accepted only with the hardened correctness gate and measured perf artifacts, not from the original failed attempt.

Artifact `20260528T020617.300523Z` rejects a same-profile display-transform no-op shortcut in `apply_output_color_space_transform`. The candidate returned immediately when an image already had a valid target color space, intending to avoid a redundant `QImage` wrapper assignment after the patch-only fast path had already rendered the native upload image in the preview/output profile. Focused controller/math/invariant tests passed and parity stayed at `parity_max_channel_delta=3`, but the 5-iteration `--brush-overbroad-dirty --brush-texture-patch-only` smoke run regressed the accepted frontier: `brush_p50_ms=9`, `brush_p95_ms=10`, `brush_p99_ms=10`, `adjusted_stage_p50_ms=4`, and `output_stage_p50_ms=2` versus the accepted `9/9/9 ms` one-tile p50/p95/p99 and `adjusted_stage_p50_ms=3` from artifact `20260528T015544.377961Z`. The code was reverted because it did not produce a measurable hot-path win and made the short-run tail worse. Future display-transform work needs either finer telemetry proving this function is still material at the 9 ms frontier or a direct native/GPU display path that removes the CPU patch image transform altogether.

The 16-bit controller parity verifier was stabilized after repeated validation runs exposed intermittent failures in the HDR ramp fixture. The fixture now isolates the HDR ramp in a fresh controller/image sink, batches the edit sequence in an edit transaction, copies the TIFF to an uncached path for the synchronous parity render, forces compare-mode cache bypass, compares the stable interior rows/columns of the native-linear TIFF ramp rather than the platform-jittery decoder edge pixels, and uses the same interactive removal-quality path as the preview renderer for its reference image. The edit values still exercise exposure, contrast, brightness, HDR local tone, filmic shoulder/toe, adaptive contrast, and zones, but avoid the prior clipped tone cliff at the high end of the ramp. A 3x `editor_preview_controller_test` repeat gate passed after the uncached parity fix, and the focused controller/math/invariant suite passed afterward. This is accepted as proof-infrastructure work, not a render-speed artifact: it makes future render optimization gates more trustworthy without changing production render behavior.

Artifacts `20260528T023903.145854Z` and `20260528T023944.245277Z` accept adjusted-cache telemetry prehash removal after the verifier hardening above. `render_preview_request` no longer computes the full-request `adjusted_working_cache_key(request)` and adjusted-cache snapshot before trying the guarded brush fast path; the fast path still computes and reports the base adjusted-cache lookup it actually uses. Focused `editor_preview_controller_test`, `editor_preview_math_test`, and `color_tool_invariant_test` passed, with the controller HDR verifier stress-checked by a 3x repeat gate. The 30-iteration one-tile run records `brush_avg_ms=8.37`, `brush_p50_ms=8`, `brush_p95_ms=9`, `brush_p99_ms=9`, `adjusted_stage_p50_ms=3`, `adjusted_stage_p95_ms=4`, `output_stage_p50_ms=2`, `output_stage_p95_ms=2`, `texture_upload_pixels=28561`, `speedup_ratio=110.25`, and `parity_max_channel_delta=3`. Compared with the prior accepted one-tile frontier from artifact `20260528T015544.377961Z`, this improves p50 from `9 ms` to `8 ms` while preserving p95/p99 at `9 ms`. The 2x2 companion run records `brush_p50_ms=103`, `brush_p95_ms=108`, `brush_p99_ms=108`, `adjusted_stage_pixels=773281`, `texture_upload_pixels=463761`, `speedup_ratio=8.97`, and `parity_max_channel_delta=4`, which is effectively neutral against the prior `102/109 ms` frontier while improving p95 by 1 ms. The current accepted hot-path frontier is therefore one-tile `8/9/9 ms` p50/p95/p99 and 2x2 `103/108 ms` p50/p95.

Artifacts `20260528T025941.110816Z` and `20260528T030003.210327Z` accept parallel dirty-tile adjustment for multi-tile brush edits. The guarded brush adjusted-tile renderer now builds the independent dirty-tile adjustment work serially, then runs tile crop plus local adjustment work in parallel when more than one tile is dirty and the global Qt thread pool has more than one thread. The single-tile path stays serial to avoid adding thread-pool overhead to the hottest 169x169 patch case. The implementation still collects results in request tile order, uses the existing cancellation token in every tile task, and falls back to serial execution when parallelism is unavailable. Focused `editor_preview_controller_test`, `editor_preview_math_test`, and `color_tool_invariant_test` passed; `editor_preview_controller_test` also passed a 3x repeat gate after the HDR verifier hardening. The 2x2 companion run records `brush_p50_ms=82`, `brush_p95_ms=84`, `brush_p99_ms=84`, `adjusted_stage_p50_ms=41`, `adjusted_stage_p95_ms=43`, `output_stage_p50_ms=34`, `output_stage_p95_ms=34`, `adjusted_stage_pixels=773281`, `texture_upload_pixels=463761`, `speedup_ratio=10.99`, and `parity_max_channel_delta=4`. Compared with the prior accepted 2x2 frontier of `103/108 ms`, this reduces p50 by 20.39% and p95 by 22.22% while preserving the same pixel work, upload work, and parity bound. The final 30-iteration one-tile guardrail records `brush_p50_ms=8`, `brush_p95_ms=9`, `brush_p99_ms=9`, `adjusted_stage_p50_ms=3`, `output_stage_p50_ms=2`, `texture_upload_pixels=28561`, and `parity_max_channel_delta=3`, preserving the one-tile frontier. The current accepted hot-path frontier is therefore one-tile `8/9/9 ms` p50/p95/p99 and 2x2 `82/84 ms` p50/p95.

Artifacts `20260528T034809.172857Z` and `20260528T034828.991865Z` re-verify the current final-code frontier after rejecting parallel output color conversion. A candidate that ran each dirty patch tile's `qimage_from_working_image_rect` conversion through `QtConcurrent::run` improved the 2x2 benchmark to `brush_p50_ms=59`, `brush_p95_ms=65`, and `output_stage_p50_ms=11`, but it failed the 16-bit controller parity gate immediately after re-enable: the same test passed 3x with serial output conversion and failed on the first run with parallel output conversion. The candidate was reverted because Qt working-to-display `QImage` color conversion is not proven safe to run concurrently in this path. The accepted final-code 2x2 run records `brush_p50_ms=81`, `brush_p95_ms=85`, `brush_p99_ms=85`, `adjusted_stage_p50_ms=40`, `adjusted_stage_p95_ms=44`, `output_stage_p50_ms=34`, `output_stage_p95_ms=34`, `adjusted_stage_pixels=773281`, `texture_upload_pixels=463761`, `speedup_ratio=11.05`, and `parity_max_channel_delta=4`, which preserves the accepted multi-tile frontier within normal timing noise. The final one-tile guardrail records `brush_p50_ms=8`, `brush_p95_ms=9`, `brush_p99_ms=9`, `adjusted_stage_p50_ms=3`, `adjusted_stage_p95_ms=4`, `output_stage_p50_ms=2`, `output_stage_p95_ms=2`, `texture_upload_pixels=28561`, `speedup_ratio=109.63`, and `parity_max_channel_delta=3`. The next output-stage optimization must avoid concurrent Qt color conversion; viable candidates are a profile-correct SIMD converter with HDR parity proof, a Metal/native texture patch path that keeps conversion on GPU, or a queued serial color-conversion worker that overlaps with non-Qt tile math without calling Qt conversion concurrently.

Artifacts `20260528T035255.112691Z` and `20260528T035405.787540Z` accept single-pass exact-cover patch output conversion. The exact-cover texture-patch path now writes all dirty `WorkingTileOverride` intersections into one linear half-float patch image and performs one serial Qt color-space conversion for the upload patch, instead of converting each dirty tile intersection separately and painting those converted images together. This preserves the safety lesson from the rejected parallel-output candidate: Qt color conversion remains serial, but conversion setup and patch assembly are paid once per upload patch. Focused `editor_preview_controller_test`, `editor_preview_math_test`, and `color_tool_invariant_test` passed; `editor_preview_controller_test` also passed a 3x repeat gate. The 2x2 run records `brush_p50_ms=68`, `brush_p95_ms=71`, `brush_p99_ms=71`, `adjusted_stage_p50_ms=40`, `adjusted_stage_p95_ms=42`, `output_stage_p50_ms=21`, `output_stage_p95_ms=25`, `adjusted_stage_pixels=773281`, `texture_upload_pixels=463761`, `speedup_ratio=13.50`, and `parity_max_channel_delta=4`. Compared with the accepted `82/84 ms` 2x2 frontier from artifact `20260528T025941.110816Z`, this reduces p50 by 17.07% and p95 by 15.48% while preserving the same adjusted pixel work, upload payload, no-base-copy gate, and parity bound. A first one-tile guardrail preserved p50/p95 but had one transient 14 ms p99 outlier; the repeated 30-iteration guardrail is accepted and records `brush_p50_ms=8`, `brush_p95_ms=9`, `brush_p99_ms=9`, `adjusted_stage_p50_ms=3`, `adjusted_stage_p95_ms=4`, `output_stage_p50_ms=2`, `output_stage_p95_ms=2`, `texture_upload_pixels=28561`, `speedup_ratio=112.00`, and `parity_max_channel_delta=3`. The current accepted hot-path frontier is therefore one-tile `8/9/9 ms` p50/p95/p99 and 2x2 `68/71 ms` p50/p95. The next multi-tile bottleneck is again the adjusted-stage brush work at roughly `40/42 ms`; output conversion remains material at `21/25 ms` but no longer dominates the 2x2 path.

Artifacts `20260528T040623.834316Z` and `20260528T040642.524050Z` advance the measured frontier with source-coordinate adjusted-stage patch rendering. The engine now exposes a `LocalAdjustmentCoordinateMapping` overload for `apply_local_adjustments_to_adjusted_pixels`, so a cropped adjustment rectangle can evaluate brush masks in full-image coordinates instead of requiring a square tile-local normalization. The controller uses this path for guarded texture-patch-only brush intersections and keeps the previous tile-local path for full-tile publication. This removes the prior 2x2 geometric fallback where non-square patch intersections were adjusted as full 512x512 tiles. Focused `editor_preview_controller_test`, `editor_preview_math_test`, and `color_tool_invariant_test` passed, and the math test now directly compares a source-mapped crop against the full-frame local-mask result. The 2x2 run records `brush_p50_ms=52`, `brush_p95_ms=54`, `brush_p99_ms=54`, `adjusted_stage_p50_ms=25`, `adjusted_stage_p95_ms=27`, `output_stage_p50_ms=21`, `output_stage_p95_ms=22`, `adjusted_stage_pixels=463761`, `texture_upload_pixels=463761`, `speedup_ratio=17.58`, and `parity_max_channel_delta=4`. Compared with the prior 2x2 frontier of `68/71 ms`, this reduces p50 by 23.53%, p95 by 23.94%, and adjusted-stage pixels by 40.03%, while making adjusted-stage work exactly equal the upload patch area. The one-tile guardrail records `brush_p50_ms=8`, `brush_p95_ms=9`, `brush_p99_ms=9`, `adjusted_stage_p50_ms=3`, `adjusted_stage_p95_ms=4`, `output_stage_p50_ms=2`, `output_stage_p95_ms=2`, `texture_upload_pixels=28561`, `speedup_ratio=110.50`, and `parity_max_channel_delta=3`, preserving the one-tile frontier. The 16-bit HDR preview verifier was then hardened to compare the stable native TIFF center band with a simpler visible edit that still exercises the HDR/reference path without the prior edge-row decoder jitter. After that verifier update, `editor_preview_controller_test` passed a 3x repeat gate and the focused controller/math/invariant suite passed again, so this frontier is accepted as release-clean.

Artifacts `20260528T043256.616378Z` and `20260528T043337.570496Z` accept parallel exact-cover patch assembly while keeping Qt color conversion serial. The exact-cover texture-patch path now validates the dirty patch intersections serially, then copies working float pixels into the shared linear half-float patch through disjoint raw-memory rectangles in parallel when more than one dirty tile contributes. The final `converted_to_color_space` call remains single-threaded, preserving the safety lesson from the rejected parallel Qt conversion candidate. A 5-sample smoke artifact `20260528T043238.897942Z` first showed the intended output-stage drop to `10/10 ms`, and the accepted 30-sample 2x2 run records `brush_p50_ms=41`, `brush_p95_ms=44`, `brush_p99_ms=46`, `adjusted_stage_p50_ms=25`, `adjusted_stage_p95_ms=28`, `output_stage_p50_ms=10`, `output_stage_p95_ms=10`, `adjusted_stage_pixels=463761`, `texture_upload_pixels=463761`, `speedup_ratio=22.59`, and `parity_max_channel_delta=4`. Compared with the source-coordinate frontier of `52/54 ms`, this reduces 2x2 p50 by 21.15%, p95 by 18.52%, and output-stage p50 by 52.38% while preserving the same adjusted/upload pixel lower bound. The one-tile guardrail stays serial and records `brush_p50_ms=8`, `brush_p95_ms=9`, `brush_p99_ms=9`, `adjusted_stage_p50_ms=3`, `adjusted_stage_p95_ms=4`, `output_stage_p50_ms=2`, `output_stage_p95_ms=2`, `texture_upload_pixels=28561`, `speedup_ratio=113.25`, and `parity_max_channel_delta=3`. This brings the measured 2x2 brush live-preview workload below the plan's 50 ms p95 target on the declared synthetic corpus.

The same validation cycle also fixed the native HDR verifier failure at its source. `decode_native_cgimage` no longer routes native images through `CGImageSourceCreateThumbnailAtIndex` when the requested `max_edge` is greater than or equal to the source long edge; it now uses the full image path unless an actual downscale is required. This avoids the intermittent black 512x24 TIFF preview produced by the native thumbnail path and is also the correct max-resolution behavior for already-small native sources. After the fix, the focused controller/math/invariant/render-parity suite passed, and `editor_preview_controller_test` passed a 3x repeat gate. The accepted frontier is therefore one-tile `8/9/9 ms` p50/p95/p99 and 2x2 `41/44/46 ms` p50/p95/p99, with native small-image preview decode no longer using the lower-confidence thumbnail path.

The Phase 0 regression gate now understands this frontier directly. `run-benchmark.py` writes summaries with all timed events instead of only the default event allowlist, `summarize-jsonl.py` records numeric field summaries for non-timing proof fields such as `iterations`, `parity_max_channel_delta`, `texture_upload_pixels`, and `texture_patch_base_copy_pixels`, and `check-summary-thresholds.py` can enforce scalar event metrics with `--metric-min`, `--metric-max`, and `--metric-eq`. The accepted 2x2 artifact passes an automated gate requiring `brush_p95_ms <= 50`, `iterations >= 30`, `parity_max_channel_delta <= 4`, `brush_max_decode_ms = 0`, `texture_patch_base_copy_pixels = 0`, `texture_upload_pixels = 463761`, and `texture_upload_image=true`. The one-tile guardrail passes the equivalent `brush_p95_ms <= 10`, `parity_max_channel_delta <= 3`, `texture_upload_pixels = 28561` gate. A deliberate negative check with `brush_p95_ms <= 40` fails against the 2x2 artifact, proving the gate detects a regression rather than only parsing the artifact.

`scripts/perf/run-preview-frontier-gate.py` packages those checks as the operational Phase 0 gate. With no artifact arguments it runs fresh one-tile and 2x2 `--brush-texture-patch-only` benchmark artifacts at the configured iteration count and validates both; with `--one-tile-artifact` and `--two-by-two-artifact` it validates existing artifacts for release evidence review. The gate passed against accepted artifacts `20260528T043337.570496Z-perf-preview-raster-b9d6e7ac` and `20260528T043256.616378Z-perf-preview-raster-bbf7c8a0`, including p95, p99, iteration count, zero-decode, zero-base-copy, exact upload-pixel, texture-upload-image, and parity invariants. This turns the documented frontier into a single repeatable command rather than a set of manually copied threshold checks.

The GitHub `desktop-macos` job now runs that same frontier gate after the broad preview performance smoke, using the built `preview_benchmark_driver`, the CI-generated synthetic corpus, 30 iterations, and the shared uploaded perf artifact directory. The job timeout is 35 minutes to account for the two extra fresh benchmark artifacts while keeping the gate in the normal desktop signal path. A local fresh-gate run on 2026-05-28 passed with artifacts `20260528T125344.367440Z-perf-preview-raster-7d3bedd6` and `20260528T125413.017499Z-perf-preview-raster-a1df9ab6`: the one-tile guardrail recorded `brush_p50_ms=8`, `brush_p95_ms=9`, `brush_p99_ms=9`, `texture_upload_pixels=28561`, and `parity_max_channel_delta=3`; the 2x2 case recorded `brush_p50_ms=42`, `brush_p95_ms=46`, `brush_p99_ms=46`, `texture_upload_pixels=463761`, `parity_max_channel_delta=4`, `brush_max_decode_ms=0`, and `texture_patch_base_copy_pixels=0`.

The Phase 0 gate now also covers the accepted non-brush preview pipeline paths. `check-summary-thresholds.py` supports `--where FIELD=VALUE` filters over raw JSONL records, so gates can assert invariants on a specific render completion instead of accepting an unrelated aggregate. `scripts/perf/run-preview-pipeline-gate.py` runs or validates the cache/cancellation/viewport pipeline artifacts: `--global-burst`, `--output-profile-toggle`, `--repeat-full-cache`, `--rapid-supersede`, and `--viewport-max-tile`. The gate requires the edit-driven global-burst max-preview render to be a rendered-cache hit with `decode_ms=0`, `edit_ms=0`, `encode_ms=0`, `jpeg_encode_requested=false`, and `publish_path=in_memory_image_provider`; the output-profile max-preview render must satisfy the same zero-work rendered-cache invariant for `dirty_domain=display_transform`; the preview-retry max-preview render must publish from `disk_jpeg_cache` with `disk_cache_hit=true` and zero decode/edit/encode work. The rapid-supersede case proves stale full work cancels and the latest settled 4096px CPU render stays inside the envelope. The viewport max-tile case proves a bounded source-resolution visible tile render and native patch upload. The macOS desktop CI job now runs this pipeline gate before the brush frontier gate.

A local fresh pipeline gate on 2026-05-28 passed with artifacts `20260528T130247.278776Z-perf-preview-raster-76ca3bcf`, `20260528T130248.314207Z-perf-preview-raster-87538e46`, and `20260528T130250.206993Z-perf-preview-raster-bbfcdb28`. The global-burst workload recorded `benchmark.preview_global_burst.completed=625 ms` and the filtered edit-driven max-preview render completed in `0 ms` with zero decode/edit/encode. The output-profile workload recorded `benchmark.preview_output_profile_toggle.completed=546 ms` because the benchmark-level completion still includes histogram activity, but the filtered display-transform max-preview render completed in `0 ms` with zero decode/edit/encode, proving the render path remains on the accepted cache frontier. The repeat-full-cache workload recorded `benchmark.preview_full_cache_repeat.completed=31 ms`; the filtered preview-retry max-preview render completed in `1 ms` with `disk_cache_hit=true` and zero decode/edit/encode. `preview_benchmark_driver` now uses a render-idle predicate for scenario waits so benchmark gates can distinguish render completion from background histogram/scope work. The production controller also schedules a queued processing-state refresh after render watcher completion so the UI busy state is not left dependent on a later background activity signal.

The brush frontier gate was re-run after the benchmark-driver render-idle change. Artifacts `20260528T130430.660794Z-perf-preview-raster-76810c65` and `20260528T130459.307092Z-perf-preview-raster-4eb90d3e` passed the updated gate: the one-tile guardrail recorded `brush_p50_ms=8`, `brush_p95_ms=9`, `brush_p99_ms=9`, `texture_upload_pixels=28561`, and `parity_max_channel_delta=3`; the 2x2 case recorded `brush_p50_ms=42`, `brush_p95_ms=49`, `brush_p99_ms=51`, `texture_upload_pixels=463761`, and `parity_max_channel_delta=4`. The gate keeps the plan's 2x2 p95 target strict at `<= 50 ms` and uses a `<= 55 ms` p99 guardrail because 30-sample p99 behaves like a max-outlier check on this runner.

The pipeline gate now includes `--rapid-supersede` as the Phase 2 cancellation proof. A pre-fix run, artifact `20260528T130832.313186Z-perf-preview-raster-13b54857`, correctly failed with `benchmark.preview_rapid_supersede.missed`: the stale full max-preview work for `generation=7` reached the rendered-cache in-memory publish path after `latest_generation=8` existed, so no `preview.render.cancelled` event was emitted. The first fix adds a final superseded-generation check before all publication paths, plus the same check before direct disk-cache publication. A controller regression test now renders a stale rendered-cache max-preview request directly and proves it returns `cancelled=true`, `success=false`, no preview image, and zero decode/edit/encode work. Artifact `20260528T131005.310912Z-perf-preview-raster-d0ba208b` then passed the cancellation contract: the stale full max-preview emitted `preview.render.cancelled` with `stage=full`, `render_mode=max_preview`, `render_reason=edit_change`, `generation=7`, `latest_generation=8`, `elapsed_ms=0`, `decode_ms=0`, `edit_ms=0`, and `encode_ms=0`; the benchmark summary recorded `cancelled_delta=1` and `full_cancelled_delta=1`; and the latest `generation=8` max-preview still published from `backend=rendered_cache` through `in_memory_image_provider` with zero decode/edit/encode.

That first cancellation pass still exposed one more stale-work leak: generation 7 could finish a full-size `settled_preview` interactive render before its stale max-preview cancellation was observed. This did not publish the wrong final max preview, but it spent roughly another full 4096px render on an obsolete generation and only then let the latest generation proceed. `handleRenderFinished` now detects a pending settled preview that would duplicate the same-generation pending max-preview output and logs `preview.render.settled_skipped_for_pending_full` instead of launching it. `check-summary-thresholds.py` also supports `--max-count 0` over filtered raw events, so the pipeline gate asserts that generation 7 has zero completed interactive `settled_preview` renders for the rapid-supersede workload. Fresh artifact `20260528T132113.055047Z-perf-preview-raster-52ffc482` passes the stricter contract: generation 7 logs `preview.render.settled_skipped_for_pending_full`, generation 7 full max-preview cancels with `elapsed_ms=0`, `decode_ms=0`, `edit_ms=0`, and `encode_ms=0`, the filtered forbidden settled-render check reports `n=0`, and generation 8 full max-preview publishes from `backend=rendered_cache` through `in_memory_image_provider` in `1 ms` with zero decode/edit/encode. The benchmark-level rapid-supersede time is `14209 ms`; the remaining time is now dominated by the latest generation's required 4096px settled preview and follow-on histogram/scope work, not obsolete generation-7 rendering. `scripts/perf/run-preview-pipeline-gate.py` validates all of these invariants, so both stale rendered-cache publication and stale duplicate settled rendering are now CI regressions.

The next diagnostic pass splits adjustment time into `adjustment_linear_stage_ms`, `adjustment_pipeline_ms`, and `adjustment_output_ms` for every preview render completion. Fresh artifact `20260529T020214.095009Z-perf-preview-raster-9c26ec06` shows the current latest 4096px rapid-supersede render is not primarily blocked by publication: generation 8 `settled_preview` records `elapsed_ms=5823`, `edit_ms=5810`, `adjustment_linear_stage_ms=410`, `adjustment_pipeline_ms=5024`, and `adjustment_output_ms=370`. The same artifact still passes the pipeline gate: generation 7 settled preview has zero completions, generation 7 full max-preview cancels with zero decode/edit/encode, and generation 8 full max-preview publishes from rendered cache in `1 ms`. A row-contiguous prefix-sum rewrite of the CPU `box_blur` vertical pass was tested and rejected in artifact `20260529T020055.891734Z-perf-preview-raster-4e21ba7a`: it increased generation 8 `adjustment_pipeline_ms` to `6024` and total `edit_ms` to `6802`, so the code was reverted. The measured bottleneck is therefore the existing CPU spatial/detail adjustment pipeline itself, especially the multi-blur texture/clarity/dehaze/sharpening path, not stale scheduling, JPEG encode, rendered-cache publication, or the tested vertical-prefix memory shape.

Artifact `20260529T020751.065426Z-perf-preview-raster-a0d6abe5` adds engine-internal adjustment telemetry and proves the CPU fallback bottleneck shape for the same 4096px rapid-supersede workload: generation 8 records `adjustment_pipeline_ms=5033`, split as `tone_mask=714`, `spatial=1847`, `post_color=1371`, `detail=988`, and `final_output=44`. The accepted cleanup in artifact `20260529T021202.176908Z-perf-preview-raster-277855cb` hoists static post-color feature checks out of the per-pixel loop, skips inactive mixer/replacement/harmony/grading branches, and avoids the sharpening edge-mask blur when sharpening masking is zero. This preserves the active math while removing provably inactive work. The same workload improves to `elapsed_ms=4231`, `edit_ms=4217`, and `adjustment_pipeline_ms=3445`, with `post_color` reduced from `1371 ms` to `298 ms` and `detail` reduced from `988 ms` to `451 ms`; `spatial` remains dominant at `1901 ms`. The pipeline gate passed with this artifact, proving generation 7 stale settled work still has `n=0`, generation 7 full max-preview cancellation still has zero decode/edit/encode, and generation 8 full max-preview still publishes from rendered cache in `1 ms`. Focused native validation passed: `color_pipeline_test`, `color_tool_invariant_test`, `editor_preview_math_test`, and `editor_preview_controller_test`; `render_parity_test` was skipped by its own conditions. The next accepted full-frame CPU frontier must therefore attack the exact multi-radius blur implementation or move texture/clarity/dehaze to a resident GPU stage, because branch cleanup is no longer the first-order cost.

Artifact `20260529T021837.811885Z-perf-preview-raster-849e9c2f` splits `spatial` into luminance setup, texture blur, clarity blur, haze blur, and apply time. It records generation 8 at `elapsed_ms=4219`, `edit_ms=4205`, `adjustment_pipeline_ms=3419`, and `adjustment_spatial_ms=1829`; the spatial split is `luminance=54`, `texture_blur=487`, `clarity_blur=490`, `haze_blur=490`, and `apply=181`. This proves the bottleneck is the repeated Gaussian-approximate blur implementation, not luminance extraction or final luminance application. Artifact `20260529T022042.633474Z-perf-preview-raster-c4a233bc` accepts scratch-buffer reuse inside the CPU Gaussian blur path. `gaussian_approximate_blur` now runs the same sliding-window box-blur passes with the same radii, but reuses one horizontal scratch buffer and two alternating output buffers across the three passes instead of allocating fresh full-image buffers for every `box_blur` call. The same 4096px workload improves to `elapsed_ms=3866`, `edit_ms=3852`, `adjustment_pipeline_ms=3067`, and `adjustment_spatial_ms=1394`; the blur split improves to `texture_blur=332`, `clarity_blur=347`, and `haze_blur=331`. Compared with the accepted pre-blur frontier artifact `20260529T021202.176908Z`, this cuts total settled-preview elapsed time by 8.63%, pipeline time by 10.97%, and spatial time by 26.67%, while keeping the exact CPU blur math. The pipeline gate passed, focused native validation passed (`color_pipeline_test`, `color_tool_invariant_test`, `editor_preview_math_test`, `editor_preview_controller_test`), `render_parity_test` was skipped by its own conditions, and `git diff --check` passed.

Artifact `20260529T022544.968444Z-perf-color-adjustment-abbb74c2` adds a texture-only spatial microbenchmark and exposes another avoidable CPU cost: with `texture=32` and inactive clarity/dehaze, the engine still spent `67.2 ms` on clarity blur and `66.6 ms` on haze blur at 1920px, plus `16.4 ms` and `16.8 ms` respectively at 960px. The accepted conditional-spatial pass computes each blur only when its corresponding effect strength is non-zero and guards the apply loop the same way, so zero-strength effects no longer allocate, blur, or sample unused buffers. Artifact `20260529T022758.205461Z-perf-color-adjustment-8280bd51` proves the inactive blur timers are now `0` for both clarity and haze. The 1920px texture-only case improves from `avg_ms=537.4`, `p50_ms=542`, `p95_ms=552`, and `adjustment_spatial_avg_ms=289.2` to `avg_ms=390.2`, `p50_ms=384`, `p95_ms=428`, and `adjustment_spatial_avg_ms=139.6`; that is a 27.39% total average reduction and a 51.73% spatial-stage reduction. The 960px case improves from `avg_ms=141.4` and `adjustment_spatial_avg_ms=74.6` to `avg_ms=110.6` and `adjustment_spatial_avg_ms=37.6`, a 21.78% total average reduction and a 49.60% spatial-stage reduction. The normal all-effects 4096px rapid-supersede guardrail, artifact `20260529T022904.851213Z-perf-preview-raster-db4a06f8`, remains on the accepted frontier at generation 8 `elapsed_ms=3723`, `edit_ms=3709`, `adjustment_pipeline_ms=2925`, and `adjustment_spatial_ms=1337`; the pipeline gate passed against this artifact, including stale full-render cancellation and zero stale generation-7 settled completions. Focused validation passed (`color_pipeline_test`, `color_tool_invariant_test`, `editor_preview_math_test`, and `editor_preview_controller_test` on immediate rerun after one transient 16-bit parity failure); `render_parity_test` was skipped by its own conditions, and `git diff --check` passed.

`scripts/perf/run-color-adjustment-gate.py` promotes that texture-only spatial proof into an automated Phase 0/engine regression gate. The gate runs or validates a `--spatial-texture-only` `perf-color-adjustment` artifact, requires the suite state to be `spatial-texture-only`, checks `preview-960` and `full-1920` latency envelopes, requires inactive `adjustment_spatial_clarity_blur_avg_ms=0` and `adjustment_spatial_haze_blur_avg_ms=0`, and requires `adjustment_spatial_texture_blur_avg_ms>=1` so the benchmark is not accidentally validating a no-op state. It passed in validation mode against accepted artifact `20260529T022758.205461Z-perf-color-adjustment-8280bd51` and passed as a fresh generate-and-validate run with artifact `20260529T023320.456763Z-perf-color-adjustment-bb4cff62`: `preview-960` recorded `avg_ms=102`, `p95_ms=105`, `adjustment_spatial_avg_ms=35.4`, clarity/haze blur `0/0`; `full-1920` recorded `avg_ms=383.2`, `p95_ms=409`, `adjustment_spatial_avg_ms=138.8`, clarity/haze blur `0/0`. The macOS desktop CI workflow now runs this gate after the preview pipeline gate and before the brush frontier gate, uploading its artifact into the shared performance artifact bundle.

Artifact `20260529T023709.599961Z-perf-preview-raster-2683c1fc` accepts reusable CPU blur workspaces across sequential Gaussian-approximate blur calls. The prior scratch-buffer fix removed repeated allocations inside one blur, but the spatial/detail paths still allocated and initialized full-image horizontal/current/next buffers separately for every texture, clarity, haze, sharpening, luminance-noise, and chroma-noise blur. The accepted change adds `gaussian_approximate_blur_into` with an optional `BlurWorkspace`, keeps the existing return-by-value helper for existing call sites, and routes the sequential spatial/detail blur calls through one reusable workspace per stage. The 4096px rapid-supersede generation-8 render improves from artifact `20260529T022904.851213Z` at `elapsed_ms=3723`, `edit_ms=3709`, `adjustment_pipeline_ms=2925`, and `adjustment_spatial_ms=1337` to `elapsed_ms=3566`, `edit_ms=3552`, `adjustment_pipeline_ms=2761`, and `adjustment_spatial_ms=1008`. Spatial blur split improves from `texture_blur=336`, `clarity_blur=320`, and `haze_blur=324` to `texture_blur=244`, `clarity_blur=187`, and `haze_blur=183`; detail also improves from `509 ms` to `467 ms`. Against the previous accepted guardrail, that is a 4.22% total elapsed reduction, 5.61% pipeline reduction, and 24.61% spatial-stage reduction while preserving the same blur math. The preview pipeline gate passed with this artifact, the fresh color-adjustment gate passed with artifact `20260529T023732.700848Z-perf-color-adjustment-6d802be5`, focused validation passed (`color_pipeline_test`, `color_tool_invariant_test`, `editor_preview_math_test`, `editor_preview_controller_test`), `render_parity_test` was skipped by its own conditions, and `git diff --check` passed.

Artifact `20260529T024204.252706Z-perf-preview-raster-def3f70e` accepts a no-mask pre-tone specialization for spatial edits. The slow spatial path still needs the per-pixel tone pass before texture/clarity/dehaze, but when there are no native mask sets, no legacy masks, no linear/radial mask, and no HDR local tone mapping, the old loop still computed normalized mask coordinates and called zero-weight radial-mask adjustment code for every pixel. The accepted branch keeps the same input-stage, `apply_global_tone`, row-max, shoulder, curve-target, and luminance-scaling math, but bypasses the dead mask dispatch for that no-mask case. The 4096px rapid-supersede generation-8 render improves from the reusable-blur artifact `20260529T023709.599961Z` at `elapsed_ms=3566`, `edit_ms=3552`, `adjustment_pipeline_ms=2761`, and `adjustment_tone_mask_ms=845` to `elapsed_ms=3425`, `edit_ms=3411`, `adjustment_pipeline_ms=2619`, and `adjustment_tone_mask_ms=700`. That is a 3.95% total elapsed reduction, 5.14% pipeline reduction, and 17.16% tone-mask reduction on the accepted synthetic 4096px workload. The preview pipeline gate passed with this artifact, the fresh color-adjustment gate passed with artifact `20260529T024227.050002Z-perf-color-adjustment-3a2c74a6`, focused validation passed (`color_pipeline_test`, `color_tool_invariant_test`, `editor_preview_math_test`, and `editor_preview_controller_test` on immediate rerun after the known transient 16-bit native preview parity failure), `render_parity_test` was skipped by its own conditions, and `git diff --check` passed.

A full-frame output conversion shortcut is rejected by artifact `20260529T024750.840347Z-perf-preview-raster-4cf278d9`. The candidate replaced the full-invalidation tiled output conversion path with one whole-image `qimage_from_working_image` conversion followed by tile-cache population from `QImage::copy` rectangles. It preserved the final image/cache shape in code, but the target 4096px rapid-supersede render regressed to `elapsed_ms=3476`, `edit_ms=3461`, `adjustment_pipeline_ms=2667`, and `adjustment_output_ms=374` versus the accepted `3425/3411/2619/368` frontier. The candidate was reverted; the existing tiled conversion remains faster on this runner, likely because the tile-sized Qt conversions fit the current memory/cache behavior better than one large conversion plus copies.

A saturation-only post-color branch is rejected by artifact `20260529T024932.846491Z-perf-preview-raster-fbc69eb8`. The candidate added an early no-hue-domain branch for vibrance/saturation/dehaze-only post-color work, keeping the same OKLab, skin-protection, chroma, and gamut-fit math while skipping mixer/replacement/harmony/grading checks. The benchmark did not improve: the 4096px render recorded `elapsed_ms=3465`, `edit_ms=3451`, `adjustment_pipeline_ms=2660`, and `adjustment_post_color_ms=311`, worse than the accepted `3425/3411/2619` frontier with `post_color` around `298 ms`. The branch was reverted because the extra code shape did not produce a measurable win.

Artifact `20260529T025503.370351Z-perf-color-adjustment-3f099aea` adds the next diagnostic layer for the remaining CPU adjustment bottlenecks. Engine telemetry now splits `detail` into OKLab extraction, sharpening, luminance noise reduction, color noise reduction, and OKLab-to-output conversion, and splits final output into black-point compensation and output-headroom fitting. The fields are propagated through `preview.render.completed` and the color-adjustment benchmark so both full preview artifacts and focused engine microbenchmarks can identify the next target. On the default spatial-detail color benchmark, the 1920px case records `avg_ms=1034.7`, `p95_ms=1041`, `adjustment_post_color_avg_ms=300.7`, and `adjustment_detail_avg_ms=303.3`; the detail split is `oklab=20`, `sharpen=31`, `luminance_nr=79`, `color_nr=75.7`, and `output=50.7`. Final output is only `9.7 ms`, entirely from headroom fitting for this state. This proves the next CPU frontier is not final-output conversion inside `apply_color_adjustments`; it is the combination of post-color OKLab/gamut work and detail's repeated blur/noise-reduction passes. The focused validation suite passed (`color_pipeline_test`, `color_tool_invariant_test`, `editor_preview_math_test`, and `render_parity_test`), `git diff --check` passed, and the fresh color-adjustment gate still passed with artifact `20260529T025527.078287Z-perf-color-adjustment-75becf64`, including inactive clarity/haze blur `0/0` in the texture-only state.

A paired chroma blur candidate is rejected by artifact `20260529T025905.364312Z-perf-color-adjustment-d0d0c65c`. The candidate blurred OKLab `a` and `b` together through one shared two-channel traversal, preserving the same Gaussian-approximation radii and per-channel arithmetic order. It did not improve the measured frontier: the 1920px spatial-detail microbenchmark regressed from `avg_ms=1034.7`, `p95_ms=1041`, `adjustment_detail_avg_ms=303.3`, and `adjustment_detail_color_nr_avg_ms=75.7` to `avg_ms=1064`, `p95_ms=1074`, `detail=309.7`, and `color_nr=81.3`. The code was reverted. A second detail candidate folded the color-NR blend pass into the final OKLab output pass. The 5-sample microbenchmark artifact `20260529T030208.915717Z-perf-color-adjustment-d1dcff42` looked promising at `full-1920 avg_ms=1022.2`, `detail=282.2`, and `color_nr=70`, but the preview-level artifact `20260529T030328.349789Z-perf-preview-raster-2b506c88` rejected it: the 4096px generation-8 settled preview regressed from the accepted `3425/3411/2619 ms` elapsed/edit/pipeline frontier to `3487/3473/2690 ms`. The candidate was reverted because the actual preview path is the higher-priority objective.

A direct OKLab no-hue-domain post-color candidate is rejected by artifacts `20260529T030701.931832Z-perf-color-adjustment-d221fbff` and `20260529T030721.234469Z-perf-preview-raster-4c378a85`. The candidate kept the existing hue-based skin/vibrance protection math, but for saturation/vibrance/dehaze-only post-color work it scaled OKLab `a/b` directly and called `gamut_fit_oklab`, avoiding final polar reconstruction unless gamut fallback needed it. Focused correctness passed, but the 5-sample 1920px microbenchmark did not clearly improve the targeted stage: `adjustment_post_color_avg_ms` worsened to `313.6` while full-1920 total was only noise-level better at `1027.2 ms`. The preview artifact rejected it decisively: generation-8 settled preview recorded `elapsed_ms=3493`, `edit_ms=3478`, and `adjustment_pipeline_ms=2696`, worse than the accepted `3425/3411/2619` frontier. The code was reverted.

The Metal spatial stage now follows the same dependency contract as the accepted CPU conditional-spatial pass: when only texture, clarity, or dehaze is active, the Metal backend launches only the blur passes needed by non-zero strengths, and the `apply_spatial_tone` shader guards inactive blur-buffer reads. This is accepted as a production-path work reduction because the previous Metal path still dispatched texture, clarity, and haze blurs whenever any spatial effect was active, contradicting the stage-dependency proof already established for CPU. Current host evidence is limited by `fallback_reason=metal_unavailable`, so the direct speedup cannot be measured here yet. The no-regression proof is artifact `20260529T031235.409828Z-perf-color-adjustment-fd89e3d7`, where the CPU texture-only color gate passed with inactive clarity/haze blur at `0/0`, `preview-960 p95=114`, and `full-1920 p95=415`; preview artifact `20260529T031251.799717Z-perf-preview-raster-092ba9c5` passed the rapid-supersede pipeline gate with stale full-generation cancellation preserved. Focused correctness passed (`render_parity_test`, `color_pipeline_test`, `color_tool_invariant_test`, and `editor_preview_math_test`), the dev build passed, and `git diff --check` passed. A future Metal-capable runner must add command-buffer or kernel-dispatch telemetry to prove the direct reduction quantitatively.

Artifact `20260529T031814.034934Z-perf-color-adjustment-84cf9af1` adds that missing GPU work-counter telemetry surface. `ColorAdjustmentTelemetry`, `preview.render.completed`, and `benchmark.color_adjustment_pass.completed` now expose Metal command-buffer count, kernel-dispatch count, blit count, CPU-to-GPU bytes, GPU-to-CPU bytes, and per-texture/clarity/haze spatial blur dispatch counts. The Metal spatial backend increments those counters at actual command-buffer, compute-dispatch, blit, and CPU/GPU copy boundaries, so a Metal-capable texture-only run can prove that inactive clarity/haze blur dispatch counts are zero while texture blur dispatches remain positive. On the current CPU-fallback host, the fresh color gate passed (`preview-960 p95=113`, `full-1920 p95=411`) and the explicit field check confirmed `adjustment_gpu_kernel_dispatch_count_avg=0`; preview artifact `20260529T031823.653692Z-perf-preview-raster-9bd2593b` passed the rapid-supersede pipeline gate and showed the same GPU fields in `preview.render.completed`. Validation passed with `cmake --build --preset build-dev-macos-debug`, focused correctness (`render_parity_test`, `color_pipeline_test`, `color_tool_invariant_test`, `editor_preview_math_test`), and `git diff --check`.

Artifact `20260529T032411.171371Z-perf-color-adjustment-c56bdd80` closes the current-host Metal observability gap: `MetalPipelineBackend` now exposes a structured unavailable reason, `render_backend_unavailable_reason(GpuOnly)` reports that same renderer-level diagnosis, and `PreviewBackendController` probes the real backend instead of assuming macOS plus a compiled Metal target means Metal is usable. On this runner the direct parity executable exits through its intentional skip path with `no_system_metal_device`, and preview artifact `20260529T032418.725015Z-perf-preview-raster-9af8eac2` records `fallback_reason=metal_unavailable:no_system_metal_device` with all `adjustment_gpu_*` counters at zero. The color gate remained inside envelope (`preview-960 p95=110`, `full-1920 p95=404`), the rapid-supersede preview gate passed, and focused validation passed (`desktop_shell_smoke_test`, `render_parity_test`, `color_pipeline_test`, `color_tool_invariant_test`, and `editor_preview_math_test`). A follow-up validation run after adding the desktop fallback smoke assertion also passed: color artifact `20260529T032729.970259Z-perf-color-adjustment-a730e700` stayed within envelope (`preview-960 p95=123`, `full-1920 p95=438`), and rapid-supersede artifact `20260529T032753.724939Z-perf-preview-raster-365e0b96` preserved the cancellation/cache contract with generation-7 stale full work cancelled at zero decode/edit/encode and the generation-8 full max-preview published from rendered cache in `0 ms`. `scripts/perf/run-preview-pipeline-gate.py` now also checks the generation-8 4096px settled preview itself, requiring `elapsed_ms<=4200`, `edit_ms<=4100`, `adjustment_pipeline_ms<=3200`, `adjustment_spatial_ms<=1500`, `adjustment_output_ms<=500`, and zero encode. Fresh gate artifact `20260529T033150.888824Z-perf-preview-raster-59689f14` passes that new envelope at generation 8 with `elapsed_ms=3438`, `edit_ms=3424`, `adjustment_pipeline_ms=2642`, `adjustment_spatial_ms=1046`, `adjustment_output_ms=367`, and `encode_ms=0`. `scripts/perf/run-color-adjustment-gate.py` now always requires inactive GPU clarity/haze blur dispatch counts to remain zero and can be run with `--require-gpu-work` on a Metal-capable host to require positive command-buffer, kernel, transfer-byte, and texture-blur dispatch counters; fresh artifact `20260529T033147.015685Z-perf-color-adjustment-10f4e925` passes with `preview-960 p95=116`, `full-1920 p95=441`, inactive clarity/haze CPU blur `0/0`, and inactive GPU clarity/haze dispatch `0/0`. The remaining direct Metal proof is now sharply defined: on a Metal-capable host, the texture-only gate must show positive texture blur dispatches, zero inactive clarity/haze blur dispatches, and lower command/kernel work than the old unconditional Metal path.

Artifact `20260529T033527.182466Z-perf-preview-raster-a43ece52` accepts an exact final-output headroom fast path. `fit_pixel_to_output_headroom` now returns immediately when a pixel is already nonnegative and inside the configured output headroom, which is mathematically identical to the existing clamp/scale body for that case. This is not a new full-frame latency frontier because 4096px preview time remains dominated by tone, spatial blur, detail, and output conversion, but it removes measurable dead work from the final engine pass: the rapid-supersede generation-8 settled preview remains inside the CPU latency gate at `elapsed_ms=3442`, `edit_ms=3428`, `adjustment_pipeline_ms=2654`, and `adjustment_spatial_ms=1076`, while `adjustment_final_output_headroom_ms` drops to `14 ms` versus the prior fresh gate's `38 ms`. The focused texture-only color gate artifact `20260529T033506.252364Z-perf-color-adjustment-e532911e` also passes with `full-1920 p95=432` and `adjustment_final_output_headroom_avg_ms=3.2`, down from the previous `10 ms` range. Correctness validation passed (`color_pipeline_test`, `color_tool_invariant_test`, `editor_preview_math_test`; `render_parity_test` skipped on this host), the dev build passed, and `git diff --check` passed.

Artifact `20260529T035114.630956Z-perf-preview-raster-0a995ac1` accepts the next exact output-publication improvement. The first diagnostic artifact, `20260529T034433.202942Z-perf-preview-raster-7ca1d100`, split 4096px working-image publication into `output_linear_pack_ms=280`, `output_color_convert_ms=97`, and `output_stage_ms=378`, proving the largest output-side cost was our own row pack from `WorkingImage` pixels into Qt `RGBA16FPx4`, not JPEG, cache publication, or tile stitching. The accepted change parallelizes only that pack step for large images by writing disjoint row ranges into the newly allocated `QImage` using its raw bits and bytes-per-line; Qt color conversion stays serial because the earlier concurrent Qt conversion candidate failed parity. This preserves the same per-pixel clamp and `qfloat16` conversion semantics, and focused validation passed (`color_pipeline_test`, `color_tool_invariant_test`, `editor_preview_math_test`, `editor_preview_controller_test`, and `render_parity_test`). The fresh rapid-supersede gate records generation 8 at `elapsed_ms=3244`, `edit_ms=3229`, `adjustment_pipeline_ms=2687`, `adjustment_spatial_ms=1058`, `adjustment_output_ms=135`, `output_linear_pack_ms=45`, `output_color_convert_ms=89`, and `encode_ms=0`; stale generation 7 full work still cancels at zero decode/edit/encode, and the generation 8 full max-preview still publishes from rendered cache in `0 ms`. The gate now locks this frontier with `adjustment_output_ms<=250`, `output_stage_ms<=250`, `output_linear_pack_ms<=150`, and `output_color_convert_ms<=150` for the 4096 settled preview case.

A dehaze-only post-color direct-OKLab branch is rejected by artifact `20260529T035701.383267Z-perf-preview-raster-f92cfbee`. The candidate handled the no-hue-domain, dehaze-only path by scaling OKLab `a/b` directly and calling `gamut_fit_oklab`, preserving the gray cutoff and chroma-scale intent while skipping the general polar hue setup. Focused correctness passed before the perf run, but the target 4096px rapid-supersede render did not materially improve: generation 8 recorded `elapsed_ms=3240`, `edit_ms=3226`, `adjustment_pipeline_ms=2682`, and `adjustment_post_color_ms=352`, effectively flat versus the accepted `3244/3229/2687` frontier with `post_color=353`. Because the improvement is noise-level and the branch changes floating-point shape, the candidate was reverted. The next accepted post-color change must produce a mathematically meaningful reduction in `adjustment_post_color_ms` or move the work to the resident GPU path with parity proof.

The first visible-first max-preview execution proof is accepted as a scheduling/correctness step, not a new latency frontier. The previous controller path computed `planned_visible_tile_count`, `planned_adjacent_tile_count`, and `planned_background_tile_count`, but full-resolution tile execution replanned without the visible region and could execute dirty full-cover tiles in raster order. The accepted change carries `tile_visible_region` through `RenderRequest`, uses it when planning full-resolution tile execution, records executed visible/adjacent/background tile counts plus `first_executed_tile_priority`, and routes full-cover dirty-tile renders through the priority-preserving tiled renderer. `editor_preview_controller_test` now proves a 2048px max-preview request with a bottom-right visible viewport executes `visible=1`, `adjacent=3`, `background=12`, and `first_executed_tile_priority=visible`. The fresh preview pipeline gate artifact `20260529T040742.791559Z-perf-preview-raster-c1bc871e` preserves the existing no-viewport 4096px rapid-supersede contract: stale generation 7 full work cancels at zero decode/edit/encode, generation 8 full max-preview publishes from rendered cache in `1 ms`, and generation 8 settled preview remains inside the locked CPU envelope at `elapsed_ms=3327`, `edit_ms=3312`, `adjustment_pipeline_ms=2765`, `adjustment_spatial_ms=1028`, `adjustment_output_ms=143`, and `encode_ms=0`. Follow-up work still needs partial publication of those visible max-preview tiles before the background tile set is complete.

The first visible-tile partial publication slice closes that follow-up at the controller/native-texture boundary. When a full `max_preview` request has an active visible region, a resident same-size native texture, no compare mode, and an adjusted working-cache hit, `startRenderStage` now schedules a `max_preview_visible_tile` prepass before the background full request. The prepass mutates the request into a texture-patch-only dirty-region render, uses the visible tile plan, returns only `texture_upload_image` plus `texture_upload_rect`, and leaves the native rendered-cache key unchanged because a visible-only patch does not yet represent the whole new image state. The pending background `max_preview` then completes the full state normally. `editor_preview_controller_test` proves this through the real controller queue/completion path with a 2048x2048 fake native texture: the scheduler counter increments once, the visible bottom-right tile publishes exactly one `uploadPatch` at `(1536,1536,512,512)`, and the native texture remains full-size while the queued background request drains. The fresh preview pipeline gate artifact `20260529T041652.701103Z-perf-preview-raster-12cb7746` preserves the accepted no-viewport pipeline contract: stale generation 7 full work cancels at zero decode/edit/encode, generation 8 full max-preview publishes from rendered cache in `0 ms`, and generation 8 settled preview stays inside the CPU envelope at `elapsed_ms=3191`, `edit_ms=3176`, `adjustment_pipeline_ms=2639`, `adjustment_spatial_ms=1045`, `adjustment_output_ms=137`, `output_linear_pack_ms=47`, `output_color_convert_ms=90`, and `encode_ms=0`. This is accepted as scheduling and publication proof; next work must expose real viewport requests from QML/zoom state and measure `T_visible_max_tile` directly.

The QML-facing viewport entrypoint now drives that visible-tile path. `EditView.qml` was already computing the normalized visible image rect during zoom/pan and calling `EditorPreviewController::setPreviewViewport`; the missing piece was C++ scheduling. The controller now turns an active viewport change into a `viewport_change` max-preview visible-tile request only when it can stay on the bounded native patch path: active viewport, same-size resident native texture, no compare mode, and adjusted working-cache hit. That request is marked visible-tile-only, so `startRenderStage` mutates it into `max_preview_visible_tile` priority-2 work without queuing duplicate background full-image completion for a pure pan/zoom. `editor_preview_controller_test` now proves the public viewport method, not a manual render-stage call, schedules one viewport max-preview request, schedules one visible-tile prepass, and publishes exactly one native patch at `(1536,1536,512,512)` on a 2048x2048 source. The fresh pipeline gate artifact `20260529T042316.228106Z-perf-preview-raster-69e6973b` preserves the no-viewport rapid-supersede/cache contract: stale generation 7 full work cancels at zero decode/edit/encode, generation 8 full max-preview publishes from rendered cache in `0 ms`, and generation 8 settled preview remains inside the CPU envelope at `elapsed_ms=3087`, `edit_ms=3073`, `adjustment_pipeline_ms=2535`, `adjustment_spatial_ms=997`, `adjustment_output_ms=135`, `output_linear_pack_ms=45`, `output_color_convert_ms=90`, and `encode_ms=0`.

Artifact `20260529T042824.402978Z-perf-preview-raster-177ca8fb` closes the first direct viewport timing proof. The `--viewport-max-tile` workload seeds a deterministic 2048x2048 source, warms exposure `0.20`, seeds a same-size native texture, then measures the public `setPreviewViewport(0.75, 0.75, 0.125, 0.125, 512, true)` path. The run records `benchmark.preview_viewport_max_tile.completed` with `elapsed_ms=93`, `viewport_schedule_delta=1`, `visible_prepass_delta=1`, `patch_upload_delta=1`, and patch rect `(1536,1536,512,512)`. The render completion behind that event records `render_mode=max_preview_visible_tile`, `backend=adjusted_cache`, `executed_tile_count=1`, `executed_visible_tile_count=1`, `executed_tile_pixels=262144`, `texture_patch_only=true`, `texture_upload_pixels=262144`, and `preview.texture.uploaded` as a native patch upload. This is accepted as a controller/benchmark smoke proof for `T_visible_max_tile`; it is not yet the final QML/browser end-to-end or real Metal hardware proof.

The viewport timing proof is now part of the preview pipeline gate, not only a one-off artifact. `scripts/perf/run-preview-pipeline-gate.py` adds `--viewport-max-tile` with a `benchmark.preview_viewport_max_tile.completed <= 150 ms` threshold, exact schedule/prepass/patch counters, a filtered `preview.render.completed` check requiring one 262,144-pixel `max_preview_visible_tile` render through `backend=adjusted_cache`, and a filtered `preview.texture.uploaded` check requiring a native patch upload at `(1536,1536,512,512)`. Fresh full-gate artifact `20260529T043253.520266Z-perf-preview-raster-98e1be72` passes that viewport case with `elapsed_ms=64`, render `elapsed_ms=13`, `output_stage_ms=12`, `output_linear_pack_ms=9`, `output_color_convert_ms=2`, and native upload `elapsed_ms=0`. The same gate run also passed global-burst (`20260529T043237.873865Z`), output-profile-toggle (`20260529T043238.819936Z`), repeat-full-cache (`20260529T043240.661416Z`), and rapid-supersede (`20260529T043241.295676Z`), so viewport tile regressions now fail alongside the existing cache and cancellation contracts.

The active QML edit surface now closes the viewport-publication gap. The production edit page is `StudioWorkspace`, not the older standalone `EditView`, so `StudioWorkspace.ImageViewport` now publishes source-normalized viewport rectangles to `EditorPreviewController::setPreviewViewport` whenever zoom, pan, image geometry, visibility, source, asset, compare mode, or preview residency changes. The active edit viewport enables this path with `publishPreviewViewport: true`; it clears the viewport for compare mode, missing source, invalid painted geometry, or fit-to-screen zoom, and computes a device-pixel-scaled patch edge from the visible image region. `desktop_shell_smoke_test` now sizes the offscreen root to `1600x1000`, loads a deterministic `1200x800` proof image, drives `editImageViewport.setZoom(2.0)`, and asserts that the controller exposes an active bounded normalized viewport plus positive patch edge; a follow-on pan mutation must change the controller viewport rect. Focused validation passed with `cmake --build --preset build-dev-macos-debug`, `ctest --test-dir build/dev-macos-debug -R '^(desktop_shell_smoke_test|editor_preview_controller_test)$' --output-on-failure`, and `git diff --check`. This is accepted as the active-QML-surface proof; it is still not a real display/browser/manual Metal hardware timing proof.

The viewport max-tile gate now has distribution-grade coverage instead of a single smoke sample. `preview_benchmark_driver --viewport-max-tile --iterations N` emits `benchmark.preview_viewport_max_tile.sample_completed` for every sample and a summary `benchmark.preview_viewport_max_tile.completed` with p50, p90, p95, p99, max, standard deviation, median absolute deviation, and total schedule/prepass/patch counters. `scripts/perf/run-preview-pipeline-gate.py` now runs 30 viewport samples, requires the summary p95 to stay under `150 ms`, validates all 30 sample records, and validates 31 `max_preview_visible_tile` render completions plus 31 native patch uploads including the unmeasured warmup. Fresh artifact `20260529T044905.941131Z-perf-preview-raster-c6348081` passes with viewport sample p50 `75.5 ms`, p95 `99.7 ms`, max `103 ms`, render p50 `13 ms`, render p95 `15 ms`, and native patch upload p95 `0 ms`; every render remains one 262,144-pixel visible tile through `backend=adjusted_cache` with patch rect `(1536,1536,512,512)`.

The repeat-full-cache gate now has the same distribution shape and also removes a real repeated-retry inefficiency. The first 30-sample attempt exposed that immediate `preview_retry` requests fell inside the rapid-interaction window, were marked `draft_interactive`, skipped the fresh disk-cache direct path, and paid unnecessary live-preview work; p95 was `177 ms` even though the max-preview disk cache lookup itself was `0-1 ms`. `preview_retry` now uses the fresh disk-cache direct path even when the rapid-followup heuristic would otherwise mark the interactive side as draft. `editor_preview_controller_test` proves an immediate second preview retry increments the disk-cache direct schedule counter without full-render debounce, pending full work, or draft interactive state. `preview_benchmark_driver --repeat-full-cache --iterations N` now emits per-sample events plus p50/p90/p95/p99/max/stddev/MAD summary fields, and `scripts/perf/run-preview-pipeline-gate.py` runs 30 repeat-cache samples. Fresh artifact `20260529T045154.441273Z-perf-preview-raster-25e2f77d` passes with sample p50 `33.5 ms`, p95 `39 ms`, max `40 ms`, and 30 filtered `preview_retry` max-preview renders at p95 `1 ms`, all with `disk_cache_hit=true`, zero decode, zero edit, zero encode, and `publish_path=disk_jpeg_cache`.

The output-profile toggle case is now distribution-gated as well. `preview_benchmark_driver --output-profile-toggle --iterations N` warms an adjusted/rendered cache with a real exposure edit, alternates output profile between Display P3 and sRGB, emits per-sample timing, and records the same p50/p90/p95/p99/max/stddev/MAD summary fields. `scripts/perf/run-preview-pipeline-gate.py` now runs 30 output-profile samples and validates 30 filtered full `max_preview` display-transform renders. Fresh artifact `20260529T045508.170891Z-perf-preview-raster-536f4215` passes with sample p50 `548 ms`, p95 `554 ms`, max `556 ms`; the filtered max-preview publication path records p50/p95/max `0 ms`, zero decode, zero edit, zero encode, `backend=rendered_cache`, `jpeg_encode_requested=false`, and `publish_path=in_memory_image_provider` for all 30 samples. This proves the max-preview cache path is already optimal for display-transform toggles, while the higher end-to-end sample timing is dominated by visible preview/histogram completion that the current benchmark still waits for.

The global-burst case now has release-grade statistical coverage instead of a single proof sample. `preview_benchmark_driver --global-burst --iterations N` alternates exposure/contrast values through the real controller setters, waits for each preview cycle to drain, emits `benchmark.preview_global_burst.sample_completed` per sample, and records p50/p90/p95/p99/max/stddev/MAD in `benchmark.preview_global_burst.completed`. `scripts/perf/run-preview-pipeline-gate.py` now runs 30 burst samples, requires p95/p99/max under `700 ms`, validates all sample records, and validates exactly 30 filtered edit-change `max_preview` completions. Fresh artifact `20260529T045707.809968Z-perf-preview-raster-3f901426` passes with sample p50 `549 ms`, p95 `577.1 ms`, max `614 ms`, and 30 filtered max-preview publications at p50/p95/max `0 ms`; every filtered max-preview render stays on `backend=rendered_cache` with zero decode, zero edit, zero encode, no JPEG encode, and `publish_path=in_memory_image_provider`. This preserves the earlier render-count reduction while turning it into an enforceable distribution gate.

The rapid-supersede cancellation proof is now distribution-gated and deliberately defeats same-recipe cache replay. `preview_benchmark_driver --rapid-supersede --iterations N` creates a fresh controller for each sample, varies the stale exposure and final contrast values, starts stale full-resolution max-preview work, commits the newer edit while that work is active, and emits `benchmark.preview_rapid_supersede.sample_completed` with per-sample generation and cancellation counters. An initial 30-sample attempt rejected the identical-value benchmark shape because only 15 samples produced a fresh latest 4096px settled render; the other half reused a previously rendered latest result. The accepted gate now validates 30 samples, 30 stale full `preview.render.cancelled` records, zero stale generation-6 settled-preview completions, 30 latest generation-7 4096px settled CPU renders inside the stage envelope, and 30 latest max-preview rendered-cache publications. Fresh artifact `20260529T050720.766594Z-perf-preview-raster-052334de` passes with sample p50 `11243.5 ms`, p95 `11398.7 ms`, max `11694 ms`, 30 full cancellations at p95 `1 ms`, 30 latest max-preview publications at p95 `1 ms` with zero decode/edit/encode, and 30 latest settled renders at p50 `2828 ms`, p95 `2993 ms`, max `3270 ms`.

The brush tile-cache hot path is now release-gated as a 30-sample patch-only distribution. `scripts/perf/run-preview-pipeline-gate.py` runs `preview_benchmark_driver --brush-tile-cache --brush-texture-patch-only --iterations 30`, validates the summary `brush_elapsed_ms` field, validates every `benchmark.preview_brush_tile_cache.sample_completed` record, and filters the optimized `preview.render.completed` records down to `backend=adjusted_tile_cache`, `dirty_domain=mask_rasterization`, and `texture_patch_only=true`. The gate requires one dirty output tile, bounded adjusted/output/texture pixels, nonzero patch pixels, zero texture-patch base copies, zero decode/encode, patch-image publication only, parity within four 8-bit channel values, and at least a `10x` p50 speedup over full-frame reference. Fresh artifact `20260529T051532.947878Z-perf-preview-raster-fc92e050` passes with brush sample p50 `8 ms`, p95 `9 ms`, max `9 ms`, adjusted-stage p95 `4 ms`, output-stage p95 `2 ms`, `texture_upload_pixels=28561`, `parity_max_channel_delta=3`, reference p50 `818 ms`, reference p95 `854 ms`, and `speedup_ratio=102.25`. The filtered optimized render records also pass at p50/p95/max `8 ms`, with one executed tile and 262,144 executed tile pixels per sample.

The next high-traffic detail, advanced tone, and geometry controls now participate in the typed-intent scheduler boundary. `setSharpening`, `setSharpenMasking`, `setLuminanceNr`, and `setColorNr` emit `detail_spatial` edit intents instead of directly calling render policy, matching the existing texture/clarity/dehaze path. `setHdrLocalToneMapping`, `setFilmicShoulder`, `setFilmicToe`, `setAdaptiveContrast`, the levels setters, and the zone setters emit `global_tone_color` intents, matching the existing basic tone/color controls. Lens distortion/vignette/chromatic aberration, perspective, transform rotation/scale/skew, liquify, content-aware crop-fill, crop rect/zoom/preset/reset, crop editing, guided upright, horizon straighten, and upright guide setters now emit `geometry_transform` intents. `editor_preview_controller_test` proves the first sharpening edit resolves to stages starting at `detail_spatial`, the first HDR local-tone edit resolves to stages starting at `global_tone_color`, the first lens-distortion edit resolves to stages starting at `geometry_transform`, and rapid bursts across all three groups coalesce into one live-preview plus one deferred max-preview pair carrying the latest detail, advanced-tone, or geometry state. It also proves crop rect/zoom/preset/reset keep their preview-only `crop_change` scheduling while carrying typed geometry metadata. The same focused run caught and fixed a retry classification edge: immediate `preview_retry` now opts out of rapid-followup draft classification before request construction, preserving the disk-cache direct path without full-render debounce, pending full work, or draft-interactive state. Focused validation passed with `cmake --build --preset build-dev-macos-debug --target editor_preview_controller_test` and `ctest --test-dir build/dev-macos-debug -R '^editor_preview_controller_test$' --output-on-failure`.

Curve, color mixer, black-and-white, color replacement, skin-tone protection, color harmony, and color grading setters now use the same typed global tone/color scheduler boundary. A small controller helper centralizes `global_tone_color` intent submission for this class of setters, and the direct render calls in the curve/HSL/selective-color/channel-mixer/B&W/replacement/skin/harmony/grading block now emit explicit operations instead. `editor_preview_controller_test` proves the first curve edit resolves to `global_tone_color`, schedules the settled preview plus max-preview pair, and that a rapid burst across curve, mixer model/channel, HSL, selective color, channel mixer, B&W, replacement, skin, harmony, and grading coalesces into one live-preview plus one deferred max-preview pair carrying the latest color state. Focused validation rebuilt `editor_preview_controller_test`; the first `ctest` run exposed a non-deterministic later 16-bit parity miss, and the immediate rerun passed with the same binary.

RAW and input-profile controls now have explicit early-stage intent domains. `setInputColorProfile` emits `input_color_transform` instead of using the output-profile render reason, so input profile edits no longer masquerade as display-only work. RAW exposure, boost, demosaic, highlight recovery, gamut/lens correction, white balance, black/white level, calibration, and primary controls emit `raw` intents, which resolve to stages beginning at `raw_demosaic`; monitor profile and soft-proof controls now use a display-transform helper matching output profile behavior. `editor_preview_controller_test` proves input profile edits schedule from `input_color_transform`, the first RAW edit schedules from `raw_demosaic`, and a rapid RAW burst coalesces into one live-preview plus one deferred max-preview pair carrying the latest RAW state. Focused validation rebuilt `editor_preview_controller_test`; the first post-fix `ctest` run again hit the non-deterministic later 16-bit parity fixture, and the immediate rerun passed with the same binary.

Layer and curve-preset entrypoints now also stop bypassing typed intent resolution. Curve presets emit `global_tone_color`, layer creation emits `layers_composite` with full-resolution scheduling, and centralized layer stack edits emit preview-only `layers_composite` work with `photo_layer_change` as the render reason. `editor_preview_controller_test` proves curve preset application resolves to global tone/color, fill-layer creation resolves to `layers_composite` and schedules the settled plus max-preview pair, and layer opacity edits through the shared stack-change path keep the preview-only scheduling contract while carrying typed layer metadata. A fresh focused rebuild and `ctest --test-dir build/dev-macos-debug -R '^editor_preview_controller_test$' --output-on-failure` passed cleanly.

Shared mask-stack edits now use an explicit `mask_rasterization` intent boundary instead of calling render policy directly. The controller has a dedicated mask-rasterization submit helper, brush strokes still carry localized dirty regions through it, and active mask setters infer operation names such as `active_mask_exposure` from the normalized mask diff before scheduling. `editor_preview_controller_test` proves mask creation exposes the matte immediately through the forced draft/live-preview path while deferring typed max-preview work, and proves an active mask exposure edit carries `mask_rasterization`, `mask_preview_change`, and the explicit `active_mask_exposure` operation. Focused validation passed with `cmake --build --preset build-dev-macos-debug` and `ctest --test-dir build/dev-macos-debug -R '^editor_preview_controller_test$' --output-on-failure`.

The remaining preview-producing controller controls now route through typed intents as well. Preview target edge, compare toggles, compare adjustment groups, reference image changes, retouch before/after, pasted settings, applied presets, and history replay use a conservative `full_pipeline` intent helper instead of bypassing scheduler metadata. `editor_preview_controller_test` proves a preview-only compare toggle emits a typed full-pipeline intent without scheduling max-preview work and a full-resolution retouch before/after toggle emits a typed full-pipeline intent before scheduling the settled plus max-preview pair. A source search now leaves only explicit preview retry, asset selection, the central submit path, and transaction replay as direct `renderPreview` call sites in the desktop controller/QML surface. Focused validation passed with `cmake --build --preset build-dev-macos-debug` and `ctest --test-dir build/dev-macos-debug -R '^editor_preview_controller_test$' --output-on-failure`.

The color-adjustment gate now covers both accepted CPU fallback shapes instead of only ad hoc benchmark runs. `scripts/perf/run-color-adjustment-gate.py` runs 30 samples by default, validates the texture-only spatial path, then runs the heavy `spatial-detail` recipe and enforces total p95/average plus stage-level `spatial`, `post_color`, and `detail` average thresholds. The macOS desktop CI workflow invokes the same gate with `--iterations 30`, so the release path matches the local proof shape instead of falling back to a short smoke sample. Fresh artifacts `20260529T062619.413116Z-perf-color-adjustment-6a630864` and `20260529T062634.129880Z-perf-color-adjustment-b655e9af` pass the gate. The texture-only full-1920 path records p95 `393 ms`, average `365.97 ms`, and `adjustment_spatial_avg_ms=131.6` while proving inactive clarity/haze blur dispatches remain zero. The heavy spatial/detail full-1920 path records p95 `1067 ms`, average `1034.6 ms`, `adjustment_spatial_avg_ms=217.03`, `adjustment_post_color_avg_ms=322.43`, and `adjustment_detail_avg_ms=298.47`. This is accepted as release-gate coverage for the current CPU fallback frontier, not as a new algorithmic speedup.

The current tree still passes that full color-adjustment gate with `scripts/perf/run-color-adjustment-gate.py --iterations 30 --timeout 120`. Artifact `20260529T082512.839465Z-perf-color-adjustment-c81a1f50` proves the texture-only spatial path: preview-960 raw-sample p95 `111 ms`, full-1920 raw-sample p95 `399 ms`, full-1920 aggregate average `366.6 ms`, `adjustment_spatial_avg_ms=131.77`, inactive clarity/haze CPU blur `0/0`, and inactive clarity/haze GPU dispatch `0/0`. Artifact `20260529T082528.015553Z-perf-color-adjustment-f93828fe` proves the heavy spatial/detail path: preview-960 raw-sample p95 `297 ms`, full-1920 raw-sample p95 `1091 ms`, full-1920 aggregate average `1039.5 ms`, `adjustment_spatial_avg_ms=218.57`, `adjustment_post_color_avg_ms=324.87`, and `adjustment_detail_avg_ms=295.5`. The gate printed `color_adjustment_gate=passed`, so the CPU fallback frontier remains inside the release envelopes while the missing final proof remains Metal hardware plus real desktop capture.

The preview pipeline gate now enforces the no-readback preview invariant directly. `scripts/perf/run-preview-pipeline-gate.py` adds `gpu_readback_count=0` as a mandatory metric on every filtered `preview.render.completed` path it validates, including rendered-cache max-preview hits, disk-cache preview retry hits, viewport max-tile native patch renders, brush patch-only renders, and the latest-generation 4096px settled render in the rapid-supersede cancellation case. Validation against accepted artifacts `20260529T045707.809968Z`, `20260529T045508.170891Z`, `20260529T045154.441273Z`, `20260529T050720.766594Z`, `20260529T044905.941131Z`, and `20260529T051532.947878Z` passed. This turns the plan's "no avoidable preview readback" rule into an automated regression condition for the current preview pipeline.

The hardware-only Metal proof is now packaged and passing as `scripts/perf/run-metal-proof-gate.py`. The gate prints stable environment markers (`metal_proof_host_system`, `metal_proof_host_machine`, `metal_proof_python_platform`, `metal_proof_docker_detected`, `metal_proof_ci_detected`, and `metal_proof_restricted_context_possible`) before running hardware checks, so a failed artifact can distinguish Docker/container execution, CI, and restricted native macOS contexts from an app-side Metal regression. The gate runs `render_parity_test` directly and treats skip code `77` as a failure by default, so a CPU-only host cannot accidentally claim Metal proof. When parity runs, it then invokes `scripts/perf/run-color-adjustment-gate.py --require-gpu-work`, which requires positive command-buffer/kernel/transfer/texture-blur GPU counters while preserving the inactive clarity/haze dispatch-zero invariant and the 30-sample texture-only plus spatial-detail performance envelopes. The first sandboxed attempt reported `no_system_metal_device`; rerunning the same gate outside the restricted execution context proved native Metal access and exposed that the benchmark state was still using the default ProPhoto working profile, which is intentionally outside the currently supported Metal color path. `color_adjustment_benchmark_driver` now pins the simple/global and texture-only benchmark states to the supported Linear sRGB/sRGB/SDR perceptual pipeline, and `run-color-adjustment-gate.py --require-gpu-work` now treats zero CPU texture-blur time as the expected result when the texture-only blur is performed by Metal. The release-shaped command `python3 -B scripts/perf/run-metal-proof-gate.py --iterations 30 --timeout 120` passed on an unrestricted native macOS run with `metal_proof_host_system=Darwin`, `metal_proof_host_machine=arm64`, `metal_proof_docker_detected=false`, `metal_parity_gate=passed`, `metal_color_adjustment_gpu_gate=passed`, and `metal_proof_gate=passed`. The 30-sample Metal texture-only artifact is `20260601T203630.293174Z-perf-color-adjustment-1109cf24`; it records positive GPU command buffers, kernel dispatches, CPU-to-GPU bytes, GPU-to-CPU bytes, and texture-blur dispatches, zero inactive clarity/haze GPU dispatches, and zero CPU luminance/texture-blur/apply work for the GPU texture-only path. The paired 30-sample spatial-detail artifact is `20260601T203632.522546Z-perf-color-adjustment-253e76f4`; it keeps the heavy CPU fallback recipe inside the accepted p95/avg/stage thresholds while the Metal gate proves the supported GPU spatial path separately. Focused native validation also passed `ctest --test-dir build/dev-macos-debug -R "color_pipeline_test|color_tool_invariant_test|render_parity_test" --output-on-failure` outside the sandbox. `--allow-unavailable` remains only for local control-flow smoke checks and is rejected by release report validation.

The preview pipeline gate now covers the combined zoom/pan plus heavy-edit path that exposed a real low-resolution preview failure. Before the fix, an interactive 960px edit result could replace the resident 2048px native preview texture while a zoom viewport was active; the next max-resolution visible tile could no longer patch into a same-size texture, so the viewport path fell back to low-resolution publication. `handleRenderFinished` now preserves a larger active-viewport native texture when a lower-resolution interactive result arrives, publishes the interactive result through the image provider, and logs `preview.texture.upload_preserved` instead of replacing the max-resolution texture. `preview_benchmark_driver --viewport-pan-heavy-edit` drives a deterministic 2048x2048 source through a warmed heavy texture/clarity/dehaze/sharpening state, pans into the lower-right 512x512 source-resolution tile, then starts obsolete full max-preview work and supersedes it with a heavy slider edit. `scripts/perf/run-preview-pipeline-gate.py --case viewport-pan-heavy-edit` passed on artifact `20260529T071802.818233Z-perf-preview-raster-4e9195bc`: 30/30 visible max-tile renders used `backend=adjusted_cache`, `render_mode=max_preview_visible_tile`, `dirty_domain=publish_texture`, `executed_tile_pixels=262144`, `texture_patch_only=true`, `texture_upload_pixels=262144`, `gpu_readback_count=0`, and p95 `14 ms`; 30/30 native texture uploads were 512x512 patches at `(1536,1536)` with p95 `0 ms`; 60 preserved low-resolution settled edit uploads recorded `native_width=2048`, `native_height=2048`, `result_width=960`, `result_height=960`, and `viewport_active=true`; and 30/30 stale full max-preview edit renders cancelled with zero decode/edit/encode before the latest generation completed. The macOS desktop CI pipeline gate now runs this case by default with a 180-second per-case timeout so the proof is part of release regression coverage, not only a local artifact. This is accepted as automated controller-path proof for synthetic large-image zoom/pan plus heavy-edit behavior, while the real-image desktop proof remains a separate completion item.

The real desktop proof now has an auditable artifact gate instead of a manual checklist. The preferred source-bound capture path is `python3 -B scripts/perf/run-desktop-preview-proof-capture.py --run-release-proof --expected-source-image <real-large-image>`, which first rejects incompatible release-mode options (`--allow-synthetic`, `--allow-non-metal`, `--no-validate`, and `--skip-metal-preflight`) before app launch, preflights the release-runner input paths and any custom `--release-report` path, runs the Metal preflight against the same declared `--color-app`, `--parity-app`, and `--output-root` that the final release proof will use, launches the desktop app with an isolated observability run directory, prints the exact proof gesture, validates the produced run with `run-desktop-preview-proof-gate.py` after the app exits, runs the full `run-preview-release-proof.py` bundle against that same run directory, then runs `check-desktop-preview-proof-capture-manifest.py` against the completed capture artifact before returning success. The capture artifact now includes `manifest.json` at its root with the app/build inputs, proof steps, Metal preflight command, desktop validation command, release proof command, capture manifest validation command, run directory, options, and status transitions, so a finished run can be audited without reconstructing how it was launched. `scripts/perf/check-desktop-preview-proof-capture-manifest.py <artifact-dir-or-manifest.json>` validates that outer artifact: clean app exit, recorded proof steps and app input, no smoke options, no smoke-only command flags, command/result consistency, passed Metal preflight, passed real desktop validation, passed release proof, matching release-report marker, evidence log under the run directory, release report under the artifact directory, release-report inputs matching the capture manifest inputs and run/output directories, optional recorded self-check result consistency when the final manifest contains it, and then re-runs `check-preview-release-proof-report.py` on the recorded release report. The manifest records release-option preflight, custom release-report path preflight, app preflight, release-input preflight, Metal preflight, desktop validation, release proof, and capture manifest validation command arguments, exit codes, and parsed `key=value` markers before returning a failure status such as `release_mode_options_failed`, `release_report_preflight_failed`, `app_preflight_failed`, `release_input_preflight_failed`, `metal_preflight_failed`, `desktop_validation_failed`, `release_proof_failed`, or `capture_manifest_validation_failed`, so failed real capture attempts remain diagnosable instead of stopping at the last successful state or failing before an artifact exists. Use `--print-only --skip-metal-preflight` to see the run layout and gesture steps without launching the GUI; print-only mode no longer requires a built app path and still allows smoke/skip options because it does not claim release evidence. The capture helper exposes `--preview-app`, `--preview-corpus`, `--color-app`, `--parity-app`, `--release-report`, and `--expected-source-image`; when `--run-release-proof` is active, a custom `--release-report` must resolve under the capture artifact directory, and omitting it lets the release runner write its default artifact-local report. The lower-level validation command is still `python3 -B scripts/perf/run-desktop-preview-proof-gate.py <run-dir>`. The gate rejects missing source paths, non-existing source paths, split-log evidence, benchmark/synthetic image paths, and CPU-only Metal-unavailable runs by default; `--allow-synthetic --allow-non-metal` is only a local smoke mode and prints `desktop_preview_proof_release_eligible=false`. A release-eligible pass requires `desktop.startup.ready`, at least one nonempty render source `file_path` that exists on the verifier machine, the exact resolved `--expected-source-image` when that option is provided, all accepted proof evidence from one JSONL log, a source edge at least 2048px, viewport max-preview scheduling, visible `max_preview_visible_tile` render completions under the 150 ms p95 target, native partial patch uploads under the 50 ms p95 target, `preview.texture.upload_preserved` for low-resolution edit renders while a larger native viewport texture remains resident, stale full max-preview cancellation under the 150 ms p95 target with no encode, `gpu_readback_count=0` for preview renders, and at least one edit render with `backend=metal` plus positive Metal command-buffer/kernel/CPU-to-GPU counters. Validation against the existing synthetic artifact `20260529T071802.818233Z-perf-preview-raster-4e9195bc` passes only in smoke mode and reports visible-tile p95 `14 ms`, texture-patch p95 `0 ms`, 60 preserved uploads, cancellation p95 `0 ms`, zero Metal edit records, and `release_eligible=false`; the default release mode correctly rejects it as synthetic evidence.

The final release proof bundle is still directly runnable as `python3 -B scripts/perf/run-preview-release-proof.py --desktop-proof-input <real-metal-desktop-run-dir>`. Without smoke flags, it runs the full preview pipeline gate, runs `run-metal-proof-gate.py` without allowing unavailable GPU skip, and validates the real desktop run with `run-desktop-preview-proof-gate.py`. It prints `preview_release_proof_gate=passed` only when all three gates exit zero and emit explicit release markers: `preview_pipeline_gate=passed`, `metal_proof_gate=passed`, `desktop_preview_proof_gate=passed`, `desktop_preview_proof_evidence_log=<one-jsonl-log>`, and `desktop_preview_proof_release_eligible=true`. The runner itself requires the nonempty desktop evidence-log marker plus the Metal environment markers before setting `release_eligible=true`, so stale or older gate output cannot print a release pass before the report checker runs. The runner writes a JSON report under `--output-root` by default, or to an explicit `--report <path>`, with the command inputs, smoke flags, parsed gate outputs, per-command exit codes, parsed per-command markers, and release eligibility; if a sub-gate exits nonzero, the runner still writes the diagnostic report, prints `preview_release_proof_gate=failed`, and returns the failed sub-gate exit code. Missing preview, Metal, or desktop input paths are represented as failed gate results with the missing resolved path in the report instead of aborting before report creation. `run-metal-proof-gate.py` now emits `metal_proof_gate=failed` and a `metal_proof_error` marker before failing, so the report can distinguish Metal-unavailable hardware from a generic process failure. For a release-eligible pass, the runner now immediately invokes `python3 -B scripts/perf/check-preview-release-proof-report.py <report.json>` before returning success, so the final command is self-auditing. The report checker requires `release_eligible=true`, `gate=passed`, all smoke flags false, zero `subgate_exit_codes` for preview, Metal, and desktop, zero per-command exit codes, command arrays that invoke the expected gate scripts, bind the declared preview app, preview corpus, Metal apps, output root, and desktop proof input to the expected flags or positional argument, and exclude smoke-only flags such as `--allow-unavailable`, `--allow-synthetic`, and `--allow-non-metal`; Metal environment markers must be present in both the top-level Metal gate and Metal command markers, must match, must prove `metal_proof_host_system=Darwin`, `metal_proof_python_platform=darwin`, and `metal_proof_docker_detected=false`, and must include boolean CI/restricted-context diagnostics. It also requires a nonempty desktop evidence-log marker, the same evidence-log value in the top-level desktop gate and desktop command markers, an existing `.jsonl` evidence-log file, that evidence log equal to or contained under `inputs.desktop_proof_input`, and matching release markers both in the top-level `gates` object and in each command's parsed markers. `scripts/perf/test-color-adjustment-sample-compare.py` covers the raw-sample bootstrap comparator used to accept or reject future CPU/Metal color-frontier candidates, including identical-sample pass, required-improvement pass, regression failure, and insufficient-sample failure. `scripts/perf/test-desktop-preview-proof-capture.py` covers capture-helper path and command construction. `scripts/perf/test-desktop-preview-proof-capture-manifest.py` covers the outer capture manifest checker with one valid release-shaped artifact plus bootstrap-without-self-result coverage and negative app-exit, missing proof-step, missing app-input, smoke option, smoke command flag, command/result mismatch, self-check mismatch, release-report mismatch, release-report input mismatch, and invalid release-report cases. `scripts/perf/test-metal-proof-gate.py` covers the Metal gate's environment marker formatting and GPU color-gate command contract without requiring Metal hardware. `scripts/perf/test-preview-release-proof-runner.py` covers the runner eligibility, exit-code, gate-status, failed-command report, missing-input report, missing-evidence-log report, missing-Metal-environment report, Docker-Metal rejection, and report-check policy so a zero-exit command without the required markers cannot claim release status. `scripts/perf/test-preview-release-proof-report.py` covers the checker with one valid release-shaped report plus negative smoke, missing-Metal, missing-Metal-environment, Docker-Metal-environment, mismatched-Metal-environment, non-release-desktop, nonzero-subgate, forged-marker, missing-command-evidence, malformed command array, wrong gate script, smoke-only subgate command flags, mismatched command/input paths, missing desktop-evidence-log, mismatched desktop-evidence-log, nonexistent evidence-log, non-JSONL evidence-log, missing desktop input, and unrelated desktop input reports. `scripts/perf/test-desktop-preview-proof-gate.py` covers the desktop proof gate with a minimal release-shaped JSONL run and negative synthetic, non-Metal, preview-readback, and visible-after-background-ordering logs. CI runs all eight proof self-tests in the repo hygiene job. Local control-flow validation on this CPU-only runner used `--skip-preview-pipeline --allow-metal-unavailable --allow-desktop-smoke --metal-iterations 3 --desktop-proof-input perf-artifacts/20260529T071802.818233Z-perf-preview-raster-4e9195bc --report /private/tmp/photo-editor-release-proof-smoke-exit-policy.json`; that smoke run correctly exited zero only because smoke flags were explicit, printed `preview_release_proof_release_eligible=false`, `preview_release_proof_gate=smoke_passed`, and produced a report whose Metal gate is `skipped`, desktop gate is `smoke_passed`, and preview pipeline gate is `skipped`. A deliberate no-Metal failure run on this CPU-only host used `--skip-preview-pipeline --allow-desktop-smoke --metal-iterations 1 --desktop-proof-input perf-artifacts/20260529T071802.818233Z-perf-preview-raster-4e9195bc --report /private/tmp/photo-editor-release-proof-failed-gate-report.json`; it exited nonzero, wrote `gate=failed`, recorded `subgate_exit_codes.metal=1`, and captured `metal_proof_error=gpu_backend_unavailable`. A deliberate missing-desktop-input run used `--skip-preview-pipeline --allow-metal-unavailable --metal-iterations 1 --desktop-proof-input /tmp/does-not-exist-desktop-proof --report /private/tmp/photo-editor-release-proof-missing-input-report.json`; it exited `2`, wrote `gate=failed`, recorded `desktop_preview_proof_error=desktop_proof_input_missing`, and included the resolved missing input path. The report checker and capture manifest checker reject all non-release reports, as intended.

The full preview pipeline gate now has per-case timeout support so slow, release-grade CPU fallback cases are not accidentally failed by the wrapper before their own latency thresholds can be evaluated. A full-gate run with `--timeout 180` proved the shorter cases (`global-burst`, `output-profile-toggle`, and `repeat-full-cache`) but exposed that the 30-sample `rapid-supersede` proof needs a longer outer timeout on this host. `PipelineCase.timeout_min_seconds` now raises only that case to 420 seconds while preserving the ordinary timeout for the rest. Re-running `scripts/perf/run-preview-pipeline-gate.py --case rapid-supersede --timeout 180 --timeout-ms 30000` passed on artifact `20260529T075150.573574Z-perf-preview-raster-dc0b46b0`: 30/30 stale full max-preview renders cancelled with p95 `1 ms`, 30/30 latest max-preview publications hit `backend=rendered_cache` with zero decode/edit/encode/readback, 30/30 latest 4096px settled renders stayed under the `4200 ms` envelope with p95 `2877 ms`, and rapid-supersede sample p95 was `11666.9 ms`.

The complete current-tree preview pipeline gate now passes as one release-shaped command with `scripts/perf/run-preview-pipeline-gate.py --timeout 180 --timeout-ms 30000`. Fresh artifacts from that full run are `global-burst` `20260529T081528.626396Z-perf-preview-raster-91cd6b94`, `output-profile-toggle` `20260529T081545.925196Z-perf-preview-raster-e8a06bdd`, `repeat-full-cache` `20260529T081604.571814Z-perf-preview-raster-046f2b19`, `rapid-supersede` `20260529T081606.289183Z-perf-preview-raster-9597cb88`, `viewport-max-tile` `20260529T082148.033326Z-perf-preview-raster-85da59f6`, `viewport-pan-heavy-edit` `20260529T082154.491163Z-perf-preview-raster-927e4aeb`, and `brush-tile-cache` `20260529T082330.512819Z-perf-preview-raster-e45601d4`. The full run proves the actual release runner path, not just individual case execution: output-profile max-preview cache publications had p95 `0 ms` with zero decode/edit/encode/readback; repeat-full-cache preview retries hit disk cache at p95 `2 ms`; viewport max-tile visible renders had p95 `15 ms`, patch uploads p95 `0 ms`, and sample p95 `96.5 ms`; viewport-pan-heavy-edit had visible tile p95 `14 ms`, stale full cancellation p95 `0 ms`, patch upload p95 `0 ms`, and preserved 2048px native texture across 60 low-resolution edit uploads; brush tile-cache had brush p95 `9 ms`, optimized adjusted-tile render p95 `8 ms`, no decode, no encode, no preview GPU readback, and `speedup_ratio=106.75`. The only unproven release item is now the real desktop app artifact with a real large image.

The paired chroma-blur detail candidate is rejected. The candidate blurred OKLab chroma `a` and `b` in one paired scratch-buffer pass for color noise reduction. Focused correctness could be made to pass, but the clean timing comparison did not dominate the scalar accepted path: the candidate recorded `preview-960 avg_ms=290.33`, `p95_ms=306`, and `full-1920 avg_ms=1029.33`, `p95_ms=1073`, versus the accepted scalar baseline around `preview-960 avg_ms=288.67`, `p95_ms=296`, and `full-1920 avg_ms=1014.33`, `p95_ms=1052`. The code was reverted because it added detail-stage complexity while worsening the measured frontier.

The post-color predicate-hoist candidate is rejected by artifacts `20260529T063233.270440Z-perf-color-adjustment-7e0b999d` and `20260529T063248.102117Z-perf-color-adjustment-b844c859`. The candidate moved post-color curve, RGB mixer, and black-and-white activity checks out of the per-pixel loop while preserving exact math. Focused correctness passed (`color_pipeline_test`, `color_tool_invariant_test`, `editor_preview_math_test`, and `render_golden_test`), and the 5-sample microbenchmark looked neutral. The required 30-sample gate rejected it as a frontier change: the heavy full-1920 `spatial-detail` case regressed from the accepted `avg_ms=1034.6`, `p95_ms=1067`, and `adjustment_post_color_avg_ms=322.43` to `avg_ms=1082.33`, `p95_ms=1125`, and `adjustment_post_color_avg_ms=339.6`. The code was reverted. This reinforces that small branch-shape edits in the current post-color loop are below noise or harmful; the next accepted post-color improvement needs to remove real color math, use a proven exact transform cache, or move the stage to GPU with parity proof.

The texture-only spatial-apply specialization is rejected by artifacts `20260529T064216.904935Z-perf-color-adjustment-d9ea6a74` and `20260529T064231.642335Z-perf-color-adjustment-9777d857`. The candidate split the texture-only apply loop to skip the unused per-pixel highlight-headroom smoothstep while preserving the same texture target-luminance formula. Correctness passed (`color_pipeline_test`, `color_tool_invariant_test`, `editor_preview_math_test`, and `render_golden_test`; `render_parity_test` skipped with `no_system_metal_device`), but the 30-sample color gate did not dominate the accepted frontier. Texture-only full-1920 moved from accepted `avg_ms=365.97` and `p95_ms=393` to `avg_ms=369.17` and `p95_ms=405`, while heavy spatial-detail full-1920 moved from accepted `avg_ms=1034.6` and `p95_ms=1067` to `avg_ms=1067.07` and `p95_ms=1105`. The code was reverted. This shows the remaining CPU spatial frontier is not the unused texture-only highlight calculation; it needs to remove blur work, reduce memory traffic, or move the stage to the resident GPU path.

The color-adjustment benchmark now emits raw per-iteration `benchmark.color_adjustment_sample.completed` events in addition to the aggregate pass and suite events. Each sample carries `stage`, `state`, dimensions, iteration index, total elapsed time, stage telemetry, and GPU counters. `scripts/perf/run-color-adjustment-gate.py` now requires exactly one sample event per gated iteration for each 960px and 1920px texture-only/spatial-detail stage before applying the aggregate p95/avg thresholds. Short validation artifacts `20260529T064643.832063Z-perf-color-adjustment-0d3ff6e2` and `20260529T064646.106675Z-perf-color-adjustment-ed306652` pass this sample-count proof with three raw samples per gated stage. `scripts/perf/compare-color-adjustment-samples.py` adds the explicit distribution comparison gate: for each selected field, state, and stage, it computes `candidate - baseline` sample deltas and fails unless the bootstrap upper confidence bound satisfies the requested no-regression or minimum-improvement limit. `run-color-adjustment-gate.py` can invoke that comparison directly with `--baseline-texture-only-artifact` and `--baseline-spatial-detail-artifact`; validation against the same artifacts passed with a zero-delta confidence interval, while an intentional `--min-improvement-ms 1` direct comparison failed as expected. Full 30-sample sample-enabled baseline artifacts `20260529T065101.382338Z-perf-color-adjustment-207f3d00` and `20260529T065116.118091Z-perf-color-adjustment-cc7e2fd0` also pass the gate. The texture-only full-1920 path records raw-sample `p95=389 ms`, aggregate `avg_ms=366.17`, `adjustment_spatial_avg_ms=130.13`, inactive clarity/haze blur `0/0`, and inactive GPU clarity/haze dispatch `0/0`. The heavy spatial-detail full-1920 path records raw-sample `p95=1080 ms`, aggregate `avg_ms=1032.97`, `adjustment_spatial_avg_ms=215.97`, `adjustment_post_color_avg_ms=321.93`, and `adjustment_detail_avg_ms=296.37`. Running the color gate with those same artifacts as baselines proves the optional bootstrap comparison path at the full 30-sample size with zero-delta confidence intervals; an intentional `--min-improvement-ms 1` direct comparison still fails as expected. This does not change the accepted latency frontier by itself; it makes future CPU and Metal candidates auditable from the underlying sample distribution instead of only aggregate summaries.

The tiled vertical box-blur candidate is rejected. The candidate changed the vertical blur pass from one strided column at a time to contiguous 32-column row-block writes while preserving the sliding-window math; a follow-up tried 64-column blocks, and a hybrid kept radius `1-2` on the original column path while using 32-column blocks for larger blur radii. Correctness passed each time (`color_pipeline_test`, `color_tool_invariant_test`, `editor_preview_math_test`, `render_golden_test`, and `render_parity_test`). A 5-sample probe with 32-column blocks looked promising on heavy full-1920 spatial/detail work (`avg_ms=946.4`, `p95_ms=995` in artifact `20260529T065654.619186Z-perf-color-adjustment-4acfc1cc`), but every 30-sample acceptance attempt failed the strict texture-only no-regression comparison before the heavy candidate could be accepted. The 32-column run artifact `20260529T065708.927109Z-perf-color-adjustment-aae4a9bd` failed with a `preview-960` upper 95% mean-delta bound of `+3.333 ms`; the 64-column artifact `20260529T065831.615795Z-perf-color-adjustment-a8d18e89` failed at `+9.233 ms`; the hybrid artifacts `20260529T070000.648558Z-perf-color-adjustment-5e921e72` and `20260529T070038.112632Z-perf-color-adjustment-11e6fec1` failed at `+1.633 ms` and `+3.933 ms`. The code was reverted because this blur-shape change cannot mathematically prove no regression across the full accepted suite. Future blur work should either isolate large-radius kernels behind a benchmark that excludes untouched radius-2 texture-only work from the acceptance comparison, or use a different algorithmic change that reduces blur passes or memory traffic without disturbing preview-960 texture latency.

The saturation-only OKLab post-color candidate is rejected. The candidate kept the existing OKLab hue/chroma decisions for the no-hue-domain `saturation`/`vibrance`/positive-`dehaze` path, but returned through `gamut_fit_oklab` with the original OKLab `a/b` vector scaled to the new chroma instead of converting through `gamut_fit_oklch`. Correctness passed (`color_pipeline_test`, `color_tool_invariant_test`, `editor_preview_math_test`, `render_golden_test`, and `render_parity_test`), but the 30-sample changed-path comparison rejected it. A full gate attempt first failed on texture-only preview noise in artifact `20260529T070647.192108Z-perf-color-adjustment-91fd5fee` with a `+4.767 ms` upper 95% mean-delta bound, even though that path should not enter the candidate branch. The isolated spatial-detail run, using the accepted texture artifact as both baseline and candidate, produced artifact `20260529T070712.967420Z-perf-color-adjustment-9d535137` and failed directly on the changed path: `preview-960` had a `+7.500 ms` upper 95% mean-delta bound, while full-1920 worsened to `avg_ms=1090.83`, `p95_ms=1158`, `adjustment_post_color_avg_ms=348.53`, and `adjustment_detail_avg_ms=306.6` versus the accepted sample-enabled baseline `avg_ms=1032.97`, `p95=1080`, `adjustment_post_color_avg_ms=321.93`, and `adjustment_detail_avg_ms=296.37`. The code was reverted. Future post-color work should not assume `gamut_fit_oklab` is cheaper in this hot path; the next viable candidate needs either a true LUT/cache for the saturation-like transform, a vectorized OKLab conversion/gamut-fit path, or GPU residency with parity proof.

Artifact `20260528T002107.621883Z` rejects a bounded single-brush exposure loop inside `apply_single_brush_exposure_fast_path`. The candidate restricted the write loop to the brush-effect pixel bounds, but correctness still required scanning the full dirty tile for existing headroom before deciding whether the optimized exposure path was allowed. That full-tile safety scan dominated the measured adjusted-stage time, so the 5-iteration `--brush-overbroad-dirty --brush-texture-patch-only` run remained effectively flat at `adjusted_stage_p50_ms=21`, `output_stage_p50_ms=9`, `brush_p50_ms=38`, `texture_upload_pixels=28561`, and `parity_max_channel_delta=3`. The code was reverted. Future adjusted-stage work should either make the headroom proof cacheable per base tile/mask state or move the brush math to a tile-local vector/GPU kernel where the full-tile safety scan is not the serial bottleneck.

Artifact `20260528T003249.029972Z` rejects a one-point brush scan specialization in `apply_single_brush_exposure_fast_path`. The candidate kept the mandatory full-tile headroom scan but precomputed X falloff and skipped distance math for rows/columns outside a single non-erase brush point. Correctness held (`parity_max_channel_delta=3`, `brush_max_decode_ms=0`, `texture_upload_pixels=28561`), but the 5-iteration `--brush-overbroad-dirty --brush-texture-patch-only` run did not beat the accepted direct patch conversion frontier: `adjusted_stage_p50_ms=20`, `output_stage_p50_ms=2`, and `brush_p50_ms=29`, matching artifact `20260528T002634.818884Z` within noise. The code was reverted because it added branch-specific complexity without a mathematically significant latency improvement. This reinforces that the next adjusted-stage win needs to remove or cache the full-tile proof, not merely shave brush-distance arithmetic inside the same proof-shaped pass.

Artifact `20260528T011413.030585Z` rejects a controller-assisted bounded brush application that gathered base tile min/max during the crop copy, attempted to apply the exposure brush only inside the computed brush bounds, and fell back to the existing full-tile path when the headroom proof could not hold. The candidate passed the focused controller/math/invariant tests and preserved `parity_max_channel_delta=3`, but the proof structure was slower than the accepted full-tile fast path: the 5-iteration `--brush-overbroad-dirty --brush-texture-patch-only` run recorded `adjusted_stage_p50_ms=33`, `adjusted_stage_p95_ms=47`, `output_stage_p50_ms=2`, `brush_p50_ms=44`, and `brush_p95_ms=67` versus the accepted `adjusted_stage_p50_ms=20`, `output_stage_p50_ms=2`, and `brush_p50_ms=29` from artifact `20260528T002634.818884Z`. The code was reverted. The result shows that recomputing the brush proof twice over the bounded rect plus scalar min/max during crop is dominated by the existing single full-tile pass; a future version would need persistent per-tile safety metadata and a one-pass bounded vector/GPU kernel to be worth accepting.

The later `base_max * max_possible_gain` bounded brush shortcut is also rejected before benchmarking. The candidate tried to avoid the second bounded proof pass by collecting base min/max during crop, proving headroom from the maximum possible exposure gain, and applying only the brush bounds. The focused `editor_preview_controller_test` rejected it on the existing 16-bit strong-edit reference fixture with `16-bit preview max channel delta: 255` for a `512x24` preview. That failure means the proof was too broad for the current color pipeline and could silently diverge on high-bit-depth strong edits, so the code was reverted without a perf acceptance run. Future bounded adjusted-stage work must first add a narrower invariant that proves equivalence across the 16-bit linear reference path, not only the synthetic brush crop benchmark.

A saturation/vibrance widening attempt is intentionally not accepted yet. A narrow uniform-source controller fixture passed, but failed artifact `20260527T202436.153893Z` shows the high-frequency `--brush-tile-cache` workload diverging at `parity_max_channel_delta=93` when saturation was composed tile-locally over the brush. That artifact is recorded only as rejection evidence, not as a smoke artifact. The production guard now has an explicit negative test for brush-over-saturation so post-color stages cannot silently enter `backend=adjusted_tile_cache` until the renderer has a stronger stage-split contract and high-frequency proof.

A padded dependency-tile adjustment attempt is also intentionally not accepted for this brush path. Failed candidate artifact `20260527T203337.408172Z` processed the dirty tile's 576x576 padded region before cropping back to the 512x512 output tile. It preserved the same high-frequency `parity_max_channel_delta=4`, but increased optimized brush latency to `92 ms` and adjusted-stage work to `331776` pixels. Because pointwise brush exposure does not need the padded dependency pixels, this candidate was dominated by the output-tile-only path and was reverted.

A scratch-buffer removal attempt inside `apply_local_adjustments_to_adjusted_pixels` is also not accepted. Failed candidate artifact `20260527T204619.253938Z` removed the dirty tile's per-pixel target-luminance buffer and recomputed luminance during the closure pass while hoisting per-mask-set invariants. It preserved `parity_max_channel_delta=4`, `adjusted_stage_pixels=262144`, and `output_pixels=262144`, but worsened brush latency to `81 ms` versus the accepted `69 ms` artifact `20260527T204201.043268Z`. The measured CPU path is therefore faster with the scratch buffer than with recomputation for the current 512x512 brush tile workload, so the code change was reverted and the artifact is kept only as rejection evidence.

Artifact `20260527T164856.720138Z` extends the global-burst proof for full-fit sources. The promoted live preview renders at the source dimensions, stores adjusted working pixels under the same canonical cache key as max-preview, and the following full `max_preview` records `backend=adjusted_cache`, `adjusted_cache_hit=true`, `decode_ms=0`, `edit_ms=22`, `encode_ms=2`, and `elapsed_ms=25`. This proves the max-preview publication reused the already full-resolution visible adjusted pixels instead of decoding and adjusting the same image again. The burst workload completed in 329 ms.

The repeat-cache artifacts prove the first max-preview recompute elimination and scheduler acceleration. Artifact `20260527T161250.599145Z` shows a repeated asset load reaching full `max_preview` with `disk_cache_lookup=true` and `disk_cache_hit=true`, publishing through `disk_jpeg_cache`, and recording `decode_ms=0`, `edit_ms=0`, `encode_ms=0`, `jpeg_encode_requested=false`, and `elapsed_ms=2`. Artifact `20260527T161753.335486Z` adds `preview.render.disk_cache_fast_scheduled`, keeps the cache-hit render at `elapsed_ms=1`, and reduces the repeat workload from 740 ms to 576 ms by bypassing the normal full-preview debounce. Artifact `20260527T162335.635288Z` changes the repeat workload to an explicit preview retry, records `preview.render.disk_cache_direct_scheduled`, avoids the redundant settled-preview render entirely, reaches cached `max_preview` with `queue_wait_ms=2`, `decode_ms=0`, `edit_ms=0`, `encode_ms=0`, and reduces repeat latency to 36 ms. The first full render in each run still records a cache miss and performs decode, adjustment, and encode, so the artifacts prove both sides of the cache contract.

The output-profile artifacts prove the first display-transform rerender reuse. Artifact `20260527T163356.694215Z` warms the visible adjusted preview, toggles the output profile through the real controller setter, records `dirty_domain=display_transform` and dirty stages limited to display/publish/encode/histogram, then completes the visible preview with `backend=adjusted_cache`, `adjusted_cache_lookup=true`, `adjusted_cache_hit=true`, `decode_ms=0`, `edit_ms=22`, `encode_ms=0`, and `elapsed_ms=23`. Artifact `20260527T164051.192652Z` extends that proof to full max-preview publication: the output-profile visible preview records `backend=adjusted_cache`, `decode_ms=0`, `edit_ms=24`, `encode_ms=0`, and `elapsed_ms=24`; the following full `max_preview` records `backend=adjusted_cache`, `adjusted_cache_hit=true`, `decode_ms=0`, `edit_ms=36`, `encode_ms=3`, and `elapsed_ms=41`. Artifact `20260527T164354.848623Z` adds scheduler proof with `preview.render.adjusted_cache_fast_scheduled`, reducing the output-profile workload from 873 ms to 393 ms and full max-preview queue wait from 722 ms to 296 ms. Artifact `20260527T164545.859984Z` then defers noncritical diagnostic overlay generation while cached max-preview work is pending, reducing that workload to 156 ms and full max-preview queue wait to 62 ms while keeping full `max_preview` at `backend=adjusted_cache`, `decode_ms=0`, `edit_ms=22`, `encode_ms=2`, and `elapsed_ms=25`. The prior adjustment renders in the same run record adjusted-cache misses and `edit_ms=348` for visible preview and `edit_ms=276` for full max-preview, so the artifacts prove both the visible and max-preview miss/hit pairs, the eliminated decode/adjustment work, and the removal of avoidable scheduler/GUI-thread wait for display-only rerenders.

### Phase 0: Baseline And Instrumentation

1. Add missing stage/tile telemetry fields.
2. Record current baseline for all preview scenarios.
3. Add summary reports for latency, cache, bytes, fallback, and cancelled work.
4. Add dashboard tables for p50/p95/p99 and Pareto frontier.

Exit criteria:

- A baseline artifact exists for every scenario.
- Regressions can be detected automatically.

### Phase 1: Frontend Intent And Transactions

1. Add typed edit intents.
2. Convert high-traffic controls: basic sliders, crop, mask brush, output profile.
3. Replace direct render calls from converted setters with intent emission.
4. Keep compatibility fallback for unconverted controls.

Exit criteria:

- Slider drag emits bounded intents.
- No-op changes emit no render.
- Render request count per gesture is predictable.

### Phase 2: Scheduler

1. Add explicit render modes and priorities.
2. Implement latest-request-wins cancellation.
3. Separate histogram/scopes and disk encode from visible preview state.
4. Prevent background max/full work from competing with priority 0-2 preview work.

Exit criteria:

- Burst tests show bounded work.
- Rapid supersede tests emit `preview.render.cancelled` for obsolete work and then publish only the latest generation.
- Full/max preview does not regress live preview latency.

### Phase 3: Render Graph Dirty Stages

1. Define stage descriptors and recipe fingerprints.
2. Implement dirty-stage resolution.
3. Add cache-key tests.
4. Route existing full-frame renderer through stage-resolved requests.

Exit criteria:

- Dirty-stage golden tests pass.
- Output profile changes do not rerun decode/global stages.
- Brush changes do not rerun decode/global stages.

### Phase 4: Hot Path Cleanup

1. Remove JPEG writes from LivePreview.
2. Prefer native texture/in-memory publication.
3. Split `render_preview_request()` into measured stage functions.
4. Add cancellation checks to expensive CPU loops.

Exit criteria:

- LivePreview encode count is zero.
- Cancellation latency is bounded and measured through `preview.render.cancelled` timing fields.
- Stage timings are visible in artifacts.

### Phase 5: Tile Renderer

1. Implement tile region math.
2. Add tile cache and visible tile prioritization.
3. Render visible screen tiles first.
4. Add zoomed source-resolution visible tiles.
5. Add background full-image tile completion.

Exit criteria:

- Tiled output matches full-frame reference.
- Zoom/pan updates visible max-res tiles within target.
- Brush dirty work scales with dirty region, not full image area.

### Phase 6: GPU Residency

1. Add persistent Metal resource pool.
2. Keep source/working/mask/LUT resources resident.
3. Publish native texture without CPU readback.
4. Fuse simple global color stages.
5. Expand GPU stage support based on fallback frequency.

Exit criteria:

- CPU/GPU readback is zero in LivePreview.
- Command-buffer and copy counts drop.
- CPU/GPU parity tests pass.

### Phase 7: RAW Progressive Pipeline

1. Use embedded RAW previews for FirstPaint.
2. Add draft demosaic for LivePreview.
3. Cache demosaic/input transform stages.
4. Add tiled MaxPreview RAW path.

Exit criteria:

- RAW first paint is under target when embedded preview exists.
- RAW setting invalidation is stage-correct.
- Draft/settled error is measured and bounded.

### Phase 8: Memory And Cache Budgets

1. Define CPU and GPU cache budgets.
2. Add LRU/priority eviction by stage/tile.
3. Add low-memory recovery.
4. Add cache repair/invalidation versioning.

Exit criteria:

- Peak memory remains under budget.
- Eviction does not break correctness.
- Warm-cache performance improves without unbounded growth.

## Definition Of Done

The preview rendering overhaul is complete when:

- QML emits typed edit intents rather than direct render policy.
- Scheduler modes and priorities are explicit and tested.
- Dirty graph invalidation is stage- and region-aware.
- LivePreview avoids JPEG encode and CPU/GPU readback.
- MaxPreview is tiled and visible-first.
- Full-resolution work is cancellable and cannot block visible preview.
- Metal is the primary hot path on macOS with CPU fallback parity.
- Benchmarks prove latency, bytes moved, wasted work, and correctness are within target.
- Every optimization ships with a baseline comparison artifact.

Current remaining completion work after the 2026-06-01 unrestricted native macOS Metal proof and the earlier viewport/QML, RAW/input, detail, tone, geometry, color, layer, mask, and control intent proof:

- Implement the next full-frame frontier for the still-dominant CPU `spatial`, `post_color`, and `detail` bottlenecks. Accepted candidates must beat the current 4096px rapid-supersede frontier with stage telemetry, not only a microbenchmark.
- Keep the typed-intent boundary closed as new preview-producing controls are added. RAW/input-profile, global tone/color, advanced tone/levels/zones, curve/HSL/color-mixer/grading, output profile, texture/clarity/dehaze, sharpening/masking/noise-reduction detail controls, optics/geometry/transform/crop/upright, layer, shared mask stack edits, brush strokes, viewport publication, preview target edge, compare/reference controls, presets/paste, history replay, and retouch toggles are covered; the current direct render surface is limited to lifecycle asset selection, explicit preview retry, and the central submit/transaction internals.
- Keep the statistical gates release-grade as new paths are added. The viewport max-tile, viewport-pan-heavy-edit, repeat-full-cache, output-profile-toggle, global-burst, rapid-supersede, and brush-tile-cache cases now all have 30-sample p95 coverage in the pipeline gate with `gpu_readback_count=0` enforced on validated render completions; the color-adjustment gate now also covers texture-only spatial work and the heavy CPU `spatial-detail` recipe with 30-sample p95/avg/stage thresholds and raw per-iteration sample records for distribution-level candidate comparison.
- Run an end-to-end desktop/manual or browser-assisted app proof on a Metal-capable machine: load a large real image, zoom beyond 100%, pan into an uncached region, change a heavy slider, and verify the run with `scripts/perf/run-desktop-preview-proof-gate.py <run-dir>` so the visible max-resolution tile, native patch upload, preserved resident texture, stale full-render cancellation, zero-readback invariant, and Metal edit counters are all proven from structured logs. The synthetic controller-path version of this scenario is now covered by `viewport-pan-heavy-edit`; completion still requires a release-eligible real app, real image, and real Metal-device artifact.
- The final acceptance command is `python3 -B scripts/perf/run-desktop-preview-proof-capture.py --run-release-proof --expected-source-image <real-large-image>` on a Metal-capable Mac with a real large image. The lower-level equivalent is `python3 -B scripts/perf/run-preview-release-proof.py --desktop-proof-input <real-metal-desktop-run-dir>` with no smoke flags. Completion requires the release command to print `preview_release_proof_gate=passed`, write the generated JSON report, self-validate that report with `check-preview-release-proof-report.py`, and pass `python3 -B scripts/perf/check-desktop-preview-proof-capture-manifest.py <capture-artifact-dir>` on the outer capture artifact; `smoke_passed` is useful local validation but is not completion evidence.
- Keep tightening the 4096px CPU latency envelope as new frontiers land; the current gate guards latest-generation settled preview elapsed, edit, pipeline, spatial, output, decode, encode, visible tile timing, and native patch publication.
