# Performance Masterclass TODO

This is the performance execution backlog for pushing the app toward its quickest practical capability. It is intentionally opinionated: measure first, optimize the path users feel, keep image quality correct, and make every speed claim reproducible.

The core workflow to optimize is:

`launch -> import -> cull -> select -> edit preview -> mask/brush -> batch export`

The final shape should be a measured system where the active visual path is always prioritized, background work is cancellable, and large libraries or long timelines change memory and queue pressure but do not change UI responsiveness.

## North Star Budgets

These are product budgets, not guesses. Adjust only after measuring real photographer workloads.

- [ ] Cold launch to usable Library: p50 under 1.5 s, p95 under 3.0 s.
- [ ] Warm launch to usable Library: p50 under 600 ms, p95 under 1.2 s.
- [ ] Library scroll: 60 fps with 25k assets, no blocking thumbnail decode on the UI thread.
- [ ] First preview after asset select: p50 under 250 ms for cached raster, under 700 ms for RAW preview.
- [ ] Slider drag response: visible draft update under 50 ms, settled high-quality preview under 250 ms.
- [ ] Brush stroke latency: input-to-preview under 16 ms for overlay feedback, under 80 ms for composited draft.
- [ ] Mask/smart-select preview: p50 under 120 ms once model/source state is warm.
- [ ] Export throughput: saturate available CPU/GPU without making the UI miss frames.
- [ ] Memory ceiling: bounded by active workspace, not by library size. No unbounded image, QML, or thumbnail growth.

## Inputs Needed To Calibrate This

The backlog below is actionable now, but these inputs make the targets precise instead of theoretical.

- [ ] Target hardware: exact Mac model, CPU/GPU, RAM, storage type, and whether external drives are common.
- [ ] Target library shape: 1k, 25k, or 100k+ assets; RAW/JPEG/HEIC/video mix; percent offline/missing.
- [ ] Target workflow priority: fast culling, heavy masking, RAW editing, batch export, or startup/library browsing.
- [ ] Representative problem files: 5 slow RAWs, 5 edited recipes, 1 slow video, and one large import folder.
- [ ] Acceptable preview approximations: which controls may use draft quality during drag, and which must be exact immediately.
- [ ] Real success bar: the latency numbers that would make the app feel instant to you on your machine.

## Operating Rules

- [ ] Every performance change must name the metric it improves and the dataset it was tested against.
- [ ] Do not optimize unknown hotspots. Add timing, profile, then change code.
- [ ] Keep preview and export visually equivalent. Any approximation must be preview-only and visibly replaced by a settled render.
- [ ] Prefer removing work over doing work faster.
- [ ] Prefer caching decoded or stage outputs over caching final UI artifacts only.
- [ ] Prefer bounded queues with cancellation over unlimited background work.
- [ ] Treat QML binding churn as a first-class performance bug.
- [ ] Keep debug observability useful but make release overhead near-zero.

## Deeper Code Audit Findings

These are concrete hotspots from the current codebase, not generic photo-editor advice.

- [ ] `apps/desktop/src/editor_preview_controller.cpp` is carrying render orchestration, decode caches, linear caches, preview encoding, smart select, masks, histogram/scope generation, and many property setters. Split performance-critical pieces into smaller services before tuning deeply.
- [x] `apps/desktop/src/editor_preview_controller.cpp` and `apps/desktop/src/photo_render_utils.cpp` both define decoded-image cache logic. Unify it so preview, thumbnails, and export share one instrumented cache.
- [ ] `renderPreview()` increments generations and clears overlay state on every request. Add a render transaction/coalescer so multi-property changes schedule once and overlay clearing happens only when affected.
- [ ] `render_preview_request()` still does decode, crop copy, adjustment, display transform, optional JPEG write, compare render, and histogram image preparation in one job. Break this into named measured stages that map to the render graph.
- [x] `handleRenderFinished()` computes histogram/scope data after applying preview results. Move histogram/scope computation into background render work or a dedicated low-priority worker so the GUI thread does not pay it after render completion.
- [x] `engine/src/pipeline/color_adjustments.cpp` uses a local `parallel_for` that creates and joins fresh `std::thread`s for many passes. Replace it with a persistent thread pool or scheduler with grain-size control.
- [ ] Several expensive CPU sections remain serial or partially serial, including LUT stack application, heal/removal mask construction, and some retouch paths. Parallelize or tile these after measuring.
- [ ] CPU cancellation exists in many loops, but it is ad hoc. Standardize a cancellation token and require every long loop, blur, retouch, LUT stack, and layer pass to check it at a bounded interval.
- [ ] `engine/src/pipeline/metal/metal_pipeline_backend.mm` repeatedly allocates `MTLBuffer`s, copies CPU pixels into shared buffers, waits synchronously, and copies pixels back. This prevents the Metal path from acting like a real resident preview pipeline.
- [ ] `MetalPipelineBackend::apply_masks_and_color_lut()` exists, but `photo_render_utils.cpp` currently calls `apply_masks()` then `apply_color_lut()` separately in the simple GPU path. Use fused GPU paths where available.
- [x] GPU eligibility in `render_output_image_with_backend()` is narrow: local mask sets, retouch masks, legacy local masks, extended range, and CPU-required state all bypass GPU. Log the exact fallback reason and prioritize the most common blockers.
- [x] Thumbnail generation is single-worker via one `QFutureWatcher`, and queue construction scans selected, visible, then all assets. Move to visible-window priority with bounded workers and asset-id maps.
- [ ] `replaceAssets` refreshes cached thumbnail state for every asset, which means file metadata checks can scale with library size on model replacement. Defer/offload that work for large catalogs.
- [ ] Rust import discovery still collects a candidate path list before processing large folders. Metadata extraction is now bounded-parallel and import database writes are chunked into transactional bulk upserts; finish true streaming discovery next.
- [ ] QML hot paths contain repeated `Image { cache: false }`, synchronous preview `Image` loading, large delegates, `Canvas` scopes, and broad repaint signals. Profile and remove work from delegates before styling more UI.
- [x] `InspectorSlider.qml` uses a `Canvas` for the track. It is fine for a few controls, but the edit workspace has many sliders; replace static/simple tracks with rectangles or pre-rendered gradient assets where possible.
- [ ] Mask overlays use multiple `Canvas` objects and repaint on broad `maskOverlayChanged` / `controlsChanged` signals. Narrow signals and throttle repaints to one per frame.
- [ ] `TimelineDocumentController` does synchronous `ffprobe` and `ffmpeg` work for duration, audio presence, posters, and waveforms. Move probe/artifact generation to bounded background workers with cached media metadata.
- [ ] Timeline state accessors rebuild `QVariantList` payloads from tracks/clips, and playback emits broad state changes. Add incremental models/signals before optimizing the QML timeline deeply.
- [ ] `TimelineView.qml` repeats poster `Image` delegates across clip width and uses `cache: false` for waveform images. Replace repeated images with a single tiled/native surface and immutable versioned URLs.
- [ ] `CarouselDocumentController` has its own image decode/render/export path. Bring it under the same cache and stage-timing discipline as photo preview/export.
- [ ] Current tests cover correctness and parity well, but there is no dedicated performance test suite or benchmark artifact path. Add one before touching risky rendering internals.

## Phase 0: Build A Real Performance Lab

- [ ] Create fixed benchmark corpora under a documented local path:
  - 100 mixed JPEG/HEIC/PNG assets.
  - 1k event shoot with similar timestamps for culling.
  - 25k catalog stress set with missing/offline paths.
  - 50 RAW files from different camera families.
  - 10 extreme recipes: masks, layers, LUTs, local tone mapping, denoise, lens correction, crop, inpaint.
- [x] Add a deterministic synthetic raster preview corpus generator for repeatable local smoke baselines.
- [x] Add a corpus-driven preview benchmark driver that loads manifest assets through `EditorPreviewController`.
- [x] Record a synthetic raster preview smoke baseline artifact and summary.
- [x] Add a benchmark runner that opens the app with a temporary runtime root and records:
  - startup ready time from `desktop.startup.ready`
  - preview latency from `preview.render.completed`
  - job latency from `job.completed`
  - catalog helper timing from helper JSONL logs
  - peak RSS and GPU memory when available
- [x] Add a script to summarize `logs/runs/*/desktop.jsonl` into p50, p95, max, and regression deltas.
- [x] Standardize event field names across desktop and Rust logs:
  - `event`
  - `workflow`
  - `asset_id` or `document_id`
  - `reason`
  - `interactive`
  - `stage`
  - `elapsed_ms`
  - `queue_wait_ms`
  - `cache_hit`
  - `bytes`
  - `thread`
  - `backend`
  - `fallback_reason`
- [x] Add benchmark presets:
  - `perf-startup`
  - `perf-library-scroll`
  - `perf-preview-raster`
  - `perf-preview-raw`
  - `perf-mask-brush`
  - `perf-export-batch`
  - `perf-timeline-scroll`
  - `perf-timeline-playback`
  - `perf-carousel-preview`
- [x] Add CI performance smoke checks with loose thresholds first, then tighten after baselines stabilize.
- [x] Store benchmark output as artifacts, not ad hoc console text.
- [x] Record the corpus path in benchmark manifests so baselines can be traced back to workload inputs.
- [x] Add a "perf contract" section to PRs: metric, before, after, workload, risk.
- [ ] Add line-item baselines for:
  - [x] `parallel_for`/scheduler overhead proxy per image pass
  - [x] CPU adjustment pass time by stage
  - [ ] Metal buffer allocation/copy/wait time
  - [ ] histogram/scope computation time
  - [ ] thumbnail queue latency for visible assets
  - [ ] catalog import discovery vs metadata vs database write time

## Phase 1: Preview Render Pipeline

Relevant code:

- `apps/desktop/src/editor_preview_controller.cpp`
- `apps/desktop/src/photo_render_utils.cpp`
- `engine/src/pipeline/render_graph.cpp`
- `engine/src/pipeline/color_adjustments.cpp`
- `engine/src/pipeline/metal/metal_pipeline_backend.mm`

Detailed execution plan:

- `docs/max-resolution-preview-rendering-plan.md`

TODO:

- [x] Unify duplicated decoded-image cache logic between `editor_preview_controller.cpp` and `photo_render_utils.cpp`.
- [x] Add cache hit/miss logging for decoded image cache and linear image cache.
- [x] Include cache memory usage in `preview.render.completed`.
- [ ] Split `render_preview_request()` into measured functions:
  - source decode
  - crop/geometry
  - linearization
  - pre-tone adjustment
  - local masks/retouch
  - spatial/detail
  - post-color/LUT
  - display transform
  - preview publish/encode
  - compare/reference render
  - histogram/scope generation
- [ ] Add recipe-stage cache keys that map to `RenderGraph` stage names instead of only full preview outputs.
- [ ] Make dirty-region invalidation real in preview execution:
  - brush strokes should not invalidate `raw_decode`, `input_transform`, or `global_tone_color`
  - output color-space changes should not invalidate pre-output stages
  - crop/transform should not recompute decode or global color
- [ ] Add a preview scheduler with explicit priorities:
  - priority 0: visible draft render for active interaction
  - priority 1: settled full preview for selected asset
  - priority 2: neighbor asset prefetch
  - priority 3: thumbnails and background cache warming
- [ ] Replace generation-only cancellation with cancellable render tokens checked inside expensive loops.
- [ ] Add cancellation checks inside CPU color, mask, local adjustment, detail, lens, and crop loops.
- [ ] Split draft and settled preview state:
  - draft uses lower edge and cheaper detail stages
  - settled render uses full preview target and exact stages
  - UI never waits for settled render during drag
- [ ] Add "reuse last good preview while new render cooks" behavior for every edit path.
- [ ] Add neighbor prefetch on selection:
  - current asset at target edge
  - next/previous two assets at draft edge
  - cancel prefetch immediately on fast navigation
- [ ] Add a no-op recipe diff fast path so controls that round to the same value do not schedule renders.
- [x] Add render coalescing so repeated property setters in one QML interaction schedule one render.
- [x] Add a scoped `beginEditTransaction()/endEditTransaction()` around operations that touch multiple controller properties.
- [ ] Replace direct `renderPreview()` setter calls with dirty flags plus one scheduled render at event-loop tail.
- [x] Do not clear overlay image/provider state unless the changed control invalidates overlay geometry or content.
- [x] Add a render reason field to requests: asset_select, slider_drag, slider_release, crop_drag, brush_drag, mask_change, compare_toggle, output_profile_change.
- [ ] Measure which controls force CPU fallback and make that visible in debug info.
- [ ] Audit `render_state_requires_cpu_pipeline` and move every feasible stage to Metal.
- [ ] Batch multiple Metal kernels into fewer command buffers where dependencies allow it.
- [ ] Add persistent GPU buffers for source, working, masks, LUTs, and output to avoid repeated CPU/GPU transfer.
- [ ] Replace per-render JPEG preview cache writes with native texture/image-provider publication whenever possible.
- [ ] For disk-backed preview artifacts, write only settled/full previews, not every interactive draft.
- [ ] Avoid QImage encode/decode for display previews when native texture path is available.
- [ ] Keep a CPU path for tests and fallback, but make GPU the hot preview path on macOS.

## Phase 2: RAW And Decode Speed

Relevant code:

- `apps/desktop/src/raw_image_decoder.h`
- `apps/desktop/src/raw_image_decoder.mm`
- `apps/desktop/src/photo_render_utils.cpp`
- `apps/desktop/src/editor_preview_controller.cpp`

TODO:

- [ ] Detect and use embedded RAW preview JPEG for first paint.
- [ ] Decode full RAW preview in the background and swap it in when ready.
- [ ] Add RAW decode timing fields:
  - file open
  - metadata/profile load
  - demosaic
  - lens correction
  - color transform
  - resize
- [ ] Cache RAW metadata and camera/lens profile lookup separately from pixels.
- [ ] Cache demosaic output by file identity, RAW settings, and preview edge.
- [ ] Add progressive preview edges: 512 -> 1024 -> target edge.
- [ ] Avoid full-resolution decode for previews unless zoom/inspection requires it.
- [ ] Add a "RAW warm path" test that selects the same file twice and verifies second preview is cache-hit dominated.
- [ ] Preload camera/lens/profile resources at idle after startup, not during first edit interaction.

## Phase 3: Catalog, Import, And Culling

Relevant code:

- `apps/desktop/src/catalog_asset_model.cpp`
- `rust/crates/catalog/src/importer.rs`
- `rust/crates/catalog/src/lib.rs`
- `rust/crates/catalog/migrations/0008_query_performance_indexes.sql`

TODO:

- [x] Add import timing for discovery and database write. Metadata extraction, thumbnail enqueue, and model refresh still need finer timing.
- [x] Replace sequential metadata extraction in `discover_assets` with bounded parallel work.
- [ ] Stream discovered assets instead of collecting the full path list before processing large folders.
- [x] Batch database inserts/upserts in transactions instead of per-asset calls.
- [ ] Add database indexes for actual query order/filter patterns:
  - capture date sort
  - import date sort
  - media type
  - rating/picked/rejected
  - project membership
  - online/offline drive filtering
- [ ] Add `EXPLAIN ANALYZE` snapshots for catalog queries used by Library and Import.
- [ ] Add keyset pagination for large catalogs instead of loading all assets into the model.
- [ ] Keep a small visible-window asset model for QML, with background page cache.
- [x] Build asset-id-to-index maps to avoid repeated full scans in selection, culling, recipe apply, and thumbnail updates.
- [x] Keep separate asset-id maps for `all_assets_` and filtered `assets_`; rebuild them only on reset/filter boundaries.
- [ ] Make survey/compare lookups use precomputed burst and duplicate groups rather than scanning all visible assets.
- [ ] Persist culling state in bulk and debounce disk/database writes during rapid keyboard review.
- [ ] Add culling benchmark: 1k left/right/rating operations with no dropped UI frames.

## Phase 4: Thumbnail System

Relevant code:

- `apps/desktop/src/catalog_asset_model.cpp`
- `apps/desktop/qml/workspaces/LibraryView.qml`
- `apps/desktop/qml/workspaces/EditView.qml`
- `apps/desktop/qml/workspaces/ImportView.qml`

TODO:

- [x] Replace single active thumbnail generation with a bounded worker pool.
- [x] Prioritize visible thumbnails over offscreen thumbnails.
- [x] Cancel thumbnail work for assets that leave the visible window.
- [x] Stop using `std::find_if` over `all_assets_` for every queued thumbnail result; resolve asset IDs through the model maps.
- [ ] Generate multi-size thumbnails:
  - tiny: list rows and dense grids
  - medium: library cards
  - large: edit filmstrip and compare
- [ ] Store dimensions, source mtime, recipe hash, and generation version in a thumbnail manifest.
- [ ] Avoid checking file metadata repeatedly from QML-visible data paths.
- [x] Move stale thumbnail file checks out of model replacement and into the thumbnail worker/manifest refresh path.
- [ ] Add stale thumbnail invalidation by source identity and recipe hash, not only mtime.
- [ ] Prewarm next/previous thumbnails during keyboard culling.
- [x] Keep QML `Image.cache` enabled for immutable thumbnail URLs; use versioned URLs when content changes.
- [x] Add a thumbnail benchmark for cold cache and warm cache.

## Phase 5: QML Frame-Time Work

Relevant code:

- `apps/desktop/qml/workspaces/LibraryView.qml`
- `apps/desktop/qml/workspaces/EditView.qml`
- `apps/desktop/qml/workspaces/MaskCanvasOverlay.qml`
- `apps/desktop/qml/workspaces/MaskView.qml`
- `apps/desktop/qml/components/*`

TODO:

- [ ] Run QML Profiler on Library scroll, Edit slider drag, Mask brush, and Timeline scroll.
- [ ] Replace heavy nested `Repeater` structures in large surfaces with virtualized `ListView` or model-backed delegates.
- [x] Audit every `cache: false`; keep it only for URLs that truly mutate in place.
- [x] Replace synchronous preview images (`asynchronous: false`) on hot surfaces with native texture or async image loading.
- [ ] Add stable delegate sizes everywhere thumbnails appear to prevent layout churn.
- [ ] Move expensive computed text and asset maps out of delegates and into model roles.
- [x] Replace repeated `localImageModel.get(index, "...")` calls in local media delegates with direct role bindings or cached delegate properties.
- [ ] Reduce duplicate fit/zoom/compare preview `Image` elements in `EditView.qml`; prefer one preview surface with explicit modes where possible.
- [ ] Move scope rendering out of inline QML `Canvas` if profiling confirms it is over budget; use a native item or precomputed raster payload.
- [ ] Avoid broad `controlsChanged` repaint fan-out for canvases; emit narrower signals for curve, scope, mask overlay, and brush state.
- [x] Throttle `Canvas.requestPaint()` during drag to one paint per frame.
- [ ] Replace decorative QML `Canvas` surfaces that repaint often with static images, shader effects, or simpler rectangles.
- [ ] Lazily load non-active workspace panels and heavy dialogs.
- [ ] Keep only active workspace and next likely workspace warm.
- [ ] Add a frame-time overlay for debug builds: UI thread ms, render thread ms, dropped frames, active render count.
- [ ] Add screenshot/perf tests for the highest-risk QML surfaces after major design changes.

## Phase 6: Mask, Brush, Retouch, And AI Interaction

Relevant code:

- `apps/desktop/src/brush_preview_compositor.cpp`
- `apps/desktop/src/brush_preview_compositor_metal.mm`
- `apps/desktop/src/brush_live_preview_item.cpp`
- `apps/desktop/src/vision_segmentation.mm`
- `apps/desktop/src/editor_preview_controller.cpp`
- `engine/src/pipeline/color_adjustments.cpp`

TODO:

- [ ] Separate instant stroke overlay from expensive mask recompute.
- [ ] Keep live brush compositing on native texture/GPU path whenever available.
- [ ] During pointer drag, update `BrushLivePreviewItem` or GPU overlay first; defer full recipe/render invalidation until release or a throttled interval.
- [ ] Commit stroke points incrementally but coalesce recipe history into one undo step per stroke.
- [ ] Use tile dirty regions for brush and local mask edits.
- [ ] Cache mask raster outputs independently from image adjustment outputs.
- [ ] Avoid rebuilding full mask preview caches when only active handle geometry, hover, or selection chrome changes.
- [ ] Add mask stack dependency tracking so editing one mask does not rebuild unrelated masks.
- [ ] Add SIMD or GPU kernels for range mask, feather, blur, and compositing.
- [ ] Add smart-select model warmup after first asset selection or when entering Mask workspace.
- [ ] Cache segmentation features per source preview edge.
- [ ] Make smart-select previews cancellable and last-pointer-wins.
- [ ] Add brush benchmark: 5 seconds of continuous stroke input at common tablet event rates.

## Phase 7: Export And Jobs

Relevant code:

- `apps/desktop/src/job_runner.cpp`
- `apps/desktop/src/export_job_controller.cpp`
- `apps/desktop/src/photo_render_utils.cpp`
- `rust/crates/jobs/src/lib.rs`

TODO:

- [ ] Add configurable job concurrency:
  - interactive mode: leave CPU/GPU headroom for UI
  - overnight mode: saturate machine
- [ ] Split export pipeline into decode, render, encode, and write stages with per-stage timing.
- [ ] Reuse the same render-stage timing vocabulary as preview so export and preview hotspots compare directly.
- [ ] Reuse preview/render caches for exports only when quality and size are valid.
- [ ] Add batch decode prefetch for sequential exports.
- [ ] Add destination disk throughput logging for large exports.
- [ ] Use bounded queues so exports do not evict active edit preview caches.
- [ ] Prioritize active preview renders over background exports.
- [ ] Add cancellation for queued and running export jobs.
- [ ] Split job execution pools by resource class:
  - preview-critical CPU/GPU
  - catalog/import IO
  - thumbnail/poster/waveform media helpers
  - export/render batch work
- [ ] Ensure long `QProcess` helpers report progress and can be cancelled or killed from job cancellation.
- [ ] Add export retry policy for transient write failures.
- [ ] Add batch export benchmark: 100 JPEG proofs and 20 archival TIFFs.

## Phase 7A: Timeline, Video, And Carousel

Relevant code:

- `apps/desktop/src/timeline_document_controller.cpp`
- `apps/desktop/src/timeline_playback_controller.cpp`
- `apps/desktop/src/carousel_document_controller.cpp`
- `apps/desktop/qml/workspaces/TimelineView.qml`
- `apps/desktop/qml/workspaces/CarouselView.qml`
- `apps/desktop/qml/components/TimelineSourcePanel.qml`

TODO:

- [ ] Add timeline timing events for document load, clip insert, clip move, trim drag, slip drag, playback seek, poster generation, waveform generation, and timeline render.
- [ ] Cache media probe results: duration, has-audio, dimensions, rotation, codec, and mtime/size identity.
- [ ] Replace synchronous `ffprobe` calls during clip creation/load with queued probe jobs and optimistic placeholders.
- [ ] Replace synchronous poster/waveform generation with a bounded worker pool shared with thumbnail policy but separate from photo previews.
- [ ] Add a timeline media artifact manifest for poster/waveform path, source identity, generation version, size, and failure reason.
- [ ] Prewarm posters and waveforms for visible timeline clips first, then adjacent clips, then offscreen clips.
- [ ] Replace wide clip poster `Repeater` image strips with one tiled texture/image provider surface.
- [ ] Keep waveform URLs immutable and versioned so QML `Image.cache` can remain enabled.
- [ ] Replace `clips()` and `sequenceTracks()` full `QVariantList` rebuilds with model-backed clip/track data for large timelines.
- [ ] Cache `orderedVideoClipRefs()` and invalidate only when track/clip structure changes.
- [ ] Maintain clip-id-to-ref and track-id-to-index maps for selection, drag, trim, markers, and playback lookup.
- [ ] During drag/trim/slip, update lightweight preview geometry every frame and commit controller state at a controlled cadence or on release.
- [ ] Narrow timeline change signals so playback position does not force unrelated clip/delegate recomputation.
- [ ] Throttle playback-to-QML position updates to display refresh cadence and avoid emitting identical rounded positions.
- [ ] Add timeline playback metrics: seek latency, dropped frames if available from Qt, media status transitions, and source switch latency.
- [ ] Keep `QMediaPlayer::setSource()` calls only when the clip source actually changes; seek within current media otherwise.
- [ ] Add a timeline render planner that estimates segments, input files, expected duration, and temporary disk use before launching ffmpeg.
- [ ] For timeline export, generate ffmpeg filter graphs off the UI thread and log graph construction time separately from encode time.
- [ ] Add progress parsing for ffmpeg render jobs so long timeline exports do not look stalled.
- [ ] Make timeline render timeout scale with estimated duration instead of a fixed 120 s.
- [ ] Bring `CarouselDocumentController` preview rendering under the shared decoded-image/linear/cache budget system.
- [ ] Add carousel preview timing for image decode, recipe render, text layout, QPainter composition, and export write.
- [ ] Debounce carousel preview invalidation by changed item/slide and avoid full document preview rebuild when only selection chrome changes.
- [ ] Cache measured text layout by text, font, box width, and scale.
- [ ] Add benchmarks:
  - 100-clip timeline scroll/zoom/drag
  - 20-minute mixed photo/video timeline render
  - 20-slide carousel preview edit
  - 20-slide carousel export

## Phase 8: Memory And Cache Discipline

TODO:

- [ ] Create one cache budget manager for decoded images, linear buffers, thumbnails, masks, LUTs, GPU buffers, and preview artifacts.
- [ ] Add per-cache metrics: entries, bytes, hit rate, evictions, oldest age.
- [ ] Make cache budgets adaptive to installed RAM and current memory pressure.
- [ ] Shrink background caches while exports run or when macOS memory pressure rises.
- [ ] Avoid duplicate CPU copies when an image is already resident as a GPU texture.
- [ ] Audit `QImage` detach/copy paths in preview and thumbnail code.
- [ ] Replace `QImage::copy` crop paths with view/tile-aware crop handling where the next stage can consume a region.
- [ ] Track memory cost for `QVariantList` histogram/scope payloads exposed to QML, especially during repeated preview updates.
- [ ] Add memory leak checks for repeated asset select, repeated slider drag, repeated workspace switching, and batch export.
- [ ] Ensure caches are invalidated on file mtime/size changes, recipe changes, output profile changes, and app version changes.
- [ ] Add a cache debug page or log summary for developers.

## Phase 9: CPU, SIMD, And Data Layout

Relevant code:

- `engine/src/pipeline/color_adjustments.cpp`
- `engine/src/pipeline/cube_lut.cpp`
- `engine/include/photo_editor/pipeline/color_adjustments.h`

TODO:

- [ ] Profile CPU pipeline with Instruments Time Profiler on representative recipes.
- [ ] Identify top five pixel loops by inclusive time.
- [ ] Replace the local `parallel_for` thread creation pattern with a persistent worker pool shared by image stages.
- [ ] Add minimum grain-size thresholds so small images and tiny tiles do not pay thread scheduling overhead.
- [ ] Add nested-parallelism protection so one expensive stage does not oversubscribe cores by calling parallel stages inside parallel work.
- [ ] Convert hot loops to cache-friendly contiguous passes.
- [ ] Fuse adjacent passes when it reduces memory bandwidth without hurting cache reuse.
- [ ] Add SIMD for per-pixel tone/color/LUT/range operations where Metal is unavailable.
- [ ] Avoid repeated curve/LUT construction inside pixel loops.
- [ ] Precompute curve data and LUTs per recipe change.
- [ ] Cache `build_color_lut`, perceptual curve LUTs, and user LUT runtime data by recipe/LUT identity.
- [ ] Parallelize user LUT stack application or move the common path to Metal.
- [ ] Parallelize heal/removal mask construction and constrain work to mask bounding boxes instead of full-frame scans.
- [ ] Use tiling for cache locality and dirty-region processing.
- [ ] Add CPU feature detection and fallback paths.
- [ ] Add microbenchmarks for LUT, curve, mask feather, local adjustment, denoise, and resize.

## Phase 10: GPU Architecture

Relevant code:

- `engine/include/photo_editor/pipeline/preview_backend.h`
- `engine/include/photo_editor/pipeline/metal/metal_pipeline_backend.h`
- `engine/src/pipeline/metal/metal_pipeline_backend.mm`
- `engine/src/pipeline/metal/color_adjustments.metal`
- `apps/desktop/src/editor_preview_texture_metal.mm`

TODO:

- [ ] Make GPU residency the default preview architecture on macOS.
- [ ] Keep source decode result, working buffer, masks, and output texture resident across edits.
- [ ] Introduce a `MetalRenderSession` or equivalent buffer-pool object for a selected asset/preview edge.
- [ ] Stop allocating fresh `MTLBuffer`s inside every `apply_*` call; reuse source, working, LUT, mask, blur, and output buffers.
- [ ] Stop calling `waitUntilCompleted` after every Metal stage when stages can be batched into one command buffer.
- [ ] Avoid CPU readback until QImage export, histogram CPU fallback, or another CPU-only stage actually requires pixels.
- [ ] Move CPU-only preview stages to Metal in priority order from profile data.
- [ ] Add GPU timing queries for each Metal kernel.
- [ ] Add GPU memory metrics to render diagnostics.
- [ ] Fuse simple tone/color kernels where memory bandwidth dominates.
- [ ] Use `apply_masks_and_color_lut()` in the hot path instead of separate mask and LUT dispatches when eligible.
- [ ] Keep high-cost spatial effects separate when fusion would reduce scheduling flexibility.
- [ ] Add fallback reason telemetry every time GPU is bypassed.
- [ ] Add CPU/GPU parity tests for every newly moved stage.
- [ ] Add a device capability matrix for integrated Apple GPUs and future CUDA/Windows support.

## Phase 11: Startup

Relevant code:

- `apps/desktop/src/main.cpp`
- `apps/desktop/src/app_bootstrap.cpp`
- `apps/desktop/qml/Main.qml`
- `apps/desktop/qml/components/StartupLoadingPopup.qml`

TODO:

- [ ] Break startup timing into app init, runtime paths, catalog service, model load, QML load, first frame, and ready.
- [ ] Defer non-critical workspace model initialization until after first frame.
- [ ] Defer carousel, timeline, playback, and other non-active workspace controller construction until the workspace is opened if dependencies allow it.
- [ ] Defer AI model checks and heavy profile loads until idle.
- [ ] Cache catalog bootstrap status and avoid redundant helper process startup.
- [ ] Do not allow `AppBootstrap` helper fallback to `cargo run` during performance runs; require a built helper binary for repeatable startup/import numbers.
- [ ] Cache or reuse the catalog DB connection URL/status and make DB bootstrap asynchronous/deferred where the UI can still become usable.
- [ ] Load only the default workspace QML at startup.
- [ ] Move startup loading animations out of expensive `Canvas` repaint loops if profiler shows cost.
- [ ] Add warm-start benchmark with existing catalog and thumbnail cache.
- [ ] Add cold-start benchmark with empty runtime root.

## Phase 12: Release Regression Gates

TODO:

- [ ] Add `ctest` labels for performance smoke tests.
- [ ] Add Rust criterion-style benchmarks for catalog import/query logic.
- [ ] Add native microbenchmarks for engine pixel kernels.
- [ ] Add a benchmark comparison command that fails only on meaningful sustained regressions.
- [ ] Keep thresholds separate for developer machines and CI hosts.
- [ ] Add perf dashboards from structured JSONL summaries.
- [ ] Require a before/after table for changes touching:
  - render graph
  - preview controller
  - image decode
  - catalog model
  - QML delegates in Library/Edit/Mask
  - job runner/export code

## Execution Order

This is the order to attack the work. Do not jump to the GPU rewrite before the measurement and scheduling layers exist.

### Track A: Measurement First

- [x] Land the JSONL summarizer and benchmark runner.
- [ ] Add render reason, interaction mode, cache stats, backend, fallback reason, queue wait, and stage timing to preview logs.
- [ ] Add import, thumbnail, timeline, carousel, and export stage timing.
- [ ] Run one baseline on the target machine and save the artifact.
- [ ] Pick the top five p95 offenders from real data.

Exit criteria:

- [ ] A single command can produce a before/after table.
- [ ] Every hot workflow has p50 and p95 numbers.
- [ ] The team can name whether the current ceiling is UI thread, CPU loops, GPU transfers, disk IO, database, helper processes, or QML bindings.

### Track B: Stop Wasted Work

- [ ] Coalesce preview renders from property setters.
- [ ] Add no-op recipe diff checks.
- [ ] Stop clearing overlay/provider state unless the change requires it.
- [ ] Make thumbnail and timeline media artifact generation visible-window prioritized.
  - Thumbnail generation is now visible-window prioritized with a bounded worker pool; timeline media artifacts still need the same policy.
- [ ] Move blocking probe/poster/waveform work off interactive paths.
- [ ] Replace repeated full scans with asset/clip ID maps.
- [x] Enable immutable QML image caching through versioned URLs.

Exit criteria:

- [ ] Slider drag and timeline drag schedule bounded work.
- [ ] Selection changes do not start stale background work that survives the next selection.
- [ ] Large catalog and large timeline operations scale with visible content first, not total content first.

### Track C: Make Active Preview Fast

- [ ] Split `render_preview_request()` into measured stages.
- [ ] Unify decoded and linear caches.
- [ ] Add real cancellation tokens through expensive CPU loops.
- [ ] Keep last-good preview visible during active renders.
- [ ] Add draft/settled preview separation with explicit quality policy.
- [x] Move histogram/scope work off the GUI thread.
- [ ] Add neighbor prefetch and cancel it aggressively during fast navigation.

Exit criteria:

- [ ] Common slider drags show visible feedback under the draft budget.
- [ ] Settled renders arrive under the product budget on representative assets.
- [ ] Cancelled renders stop consuming meaningful CPU/GPU after they are obsolete.

### Track D: Fix CPU And Memory Architecture

- [ ] Replace per-call `std::thread` spawning with a persistent worker pool.
- [ ] Add tiling, grain-size thresholds, and nested-parallelism protection.
- [ ] Cache curve/LUT/runtime structures by recipe identity.
- [ ] Remove unnecessary `QImage` copies and CPU/GPU round trips.
- [ ] Add a cache budget manager with pressure response.

Exit criteria:

- [ ] CPU render time improves on the top measured loops without increasing memory unpredictably.
- [ ] Repeated select/edit/workspace-switch tests show stable RSS.
- [ ] The same cache metrics explain preview, thumbnail, carousel, and export behavior.

### Track E: Make Metal The Hot Path

- [ ] Add GPU fallback telemetry first.
- [ ] Introduce a Metal render session/buffer pool.
- [ ] Batch kernels into fewer command buffers.
- [ ] Use fused mask/LUT paths.
- [ ] Move the most common CPU fallback blockers to Metal in profile order.
- [ ] Add CPU/GPU parity tests for every moved stage.

Exit criteria:

- [ ] Metal preview avoids repeated buffer allocation/copy/wait cycles for normal edits.
- [ ] CPU readback happens only at explicit boundaries.
- [ ] GPU and CPU outputs remain within defined parity tolerances.

### Track F: Scale The Library And Jobs

- [ ] Stream import discovery.
- [x] Parallelize metadata extraction with bounded workers.
- [x] Batch database writes.
- [ ] Add query indexes and keyset pagination.
- [ ] Split job pools by resource class and add cancellation/progress.
- [ ] Prevent exports from evicting active preview caches.

Exit criteria:

- [ ] Import throughput improves without making the UI less responsive.
- [ ] Library scrolling remains bounded by visible delegates and thumbnail availability.
- [ ] Batch export can saturate resources in overnight mode and preserve UI headroom in interactive mode.

## Immediate Highest-Leverage Sprint

Do these first.

- [x] Add the JSONL performance summarizer for `preview.render.completed`, `preview.histogram.completed`, `desktop.startup.ready`, and `job.completed`.
- [x] Record cache hit/miss and cache bytes in preview render logs.
- [x] Add render reason and interaction mode to every preview request.
- [x] Add backend and GPU fallback reason fields to preview/export logs.
- [x] Add queue wait and cancellation reason fields to preview, thumbnail, timeline artifact, and export jobs.
- [ ] Build the preview benchmark corpus and run baseline numbers.
- [x] Add the synthetic raster preview corpus generator and wire corpus paths into benchmark artifacts.
- [x] Add the preview benchmark driver for synthetic raster baseline collection.
- [x] Record a two-asset synthetic raster preview smoke baseline.
- [x] Unify the duplicated decoded-image caches.
- [x] Add the render coalescer so repeated property setters schedule one render.
- [x] Move histogram/scope computation to an async worker and log `preview.histogram.completed`.
- [x] Add real cancellation checks inside CPU render loops.
- [x] Make thumbnail generation visible-window prioritized with a bounded worker pool.
- [ ] Make `discover_assets` stream discovered assets instead of collecting the full candidate list.
- [x] Parallelize `discover_assets` metadata extraction with bounded workers.
- [ ] Audit QML `cache: false` and `asynchronous: false` on hot image delegates.
- [ ] Move timeline poster/waveform/probe work out of synchronous controller paths.
- [ ] Replace timeline repeated poster image strips with a single tiled surface.
- [ ] Add a frame-time overlay for debug builds.

## Definition Of Done

A performance item is not done until:

- [ ] The metric is captured in structured logs or a benchmark artifact.
- [ ] The before/after result is recorded.
- [ ] The tested corpus is named.
- [ ] Image-quality parity is covered when render output can change.
- [ ] Memory impact is bounded and documented.
- [ ] Cancellation and UI responsiveness are tested for long-running work.
- [ ] The result improves the user-visible workflow, not just a synthetic microcase.
