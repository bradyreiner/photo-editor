# Masking Performance Improvements

This checklist tracks concrete performance work for selection, brush, smart selection, mask preview, outline rendering, and retouch/inpaint masking. Items marked done have implementation and local validation evidence in the current worktree.

## Implemented

- [x] Stop normal `overlay` mode from drawing the raster matte image layer; use clean canvas/outline overlays instead.
- [x] Stop `edge`, `refinement-edge`, and `marching-ants` modes from drawing the raster matte image layer.
- [x] Remove settled raster matte image display from the editor canvas; explicit inspection modes now use the same clean vector fill/outline overlay instead of artifact-prone preview PNGs.
- [x] Avoid reading `activeMaskPreviewSource` from `SharedMaskOverlay`; settled mask previews now render through vector fill/outline only.
- [x] Avoid binding hidden `Image.source` to `activeMaskPreviewSource` in `SharedMaskOverlay`.
- [x] Avoid binding hidden `Image.source` to `activeMaskPreviewSource` in `MaskCanvasOverlay`.
- [x] Preserve clean canvas overlays while skipping raster matte image generation in normal overlay mode.
- [x] Make `activeMaskPreviewSource()` return empty for non-raster preview modes so accidental property reads cannot trigger matte generation.
- [x] Use fast brush-stroke sample outlines for ordinary brush masks before falling back to expensive matte compositing.
- [x] Keep expensive composite/refinement outline generation only for masks with active refinement controls.
- [x] Reuse one QML outline-path binding per overlay component instead of asking the controller for outline paths separately for visibility and paint.
- [x] Keep live brush visual feedback independent of the settled preview render path.
- [x] Keep pending brush commit points visible while the background raster commit finishes.
- [x] Move brush raster commit work off the UI thread through the existing `QtConcurrent` commit path.
- [x] Move smart selection commit work off the UI thread through the existing async commit path.
- [x] Add controller-level caching for fast outline paths keyed by mask work revision, active mask id, preview mode, and refinement state.
- [x] Add telemetry counters for skipped matte-source generation in overlay/outline modes.
- [x] Coalesce repeated `maskOverlayChanged` canvas repaints during one event loop tick.
- [x] Throttle marching-ants animation by outline point count and viewport visibility.
- [x] Disable marching-ants animation automatically for very large traced outlines.
- [x] Cache normalized outline paths in QML after controller paths change instead of normalizing inside every paint.
- [x] Add a maximum point/path budget for generated raster outlines before exposing them to QML.
- [x] Drop tiny secondary traced raster contours so noisy mask speckles do not render as colored outline artifacts.
- [x] Split brush cursor repaint from mask overlay repaint with a cursor item positioned by hover bindings instead of canvas redraws.
- [x] Avoid clearing and redrawing full overlay canvases when only the brush cursor moved.
- [x] Cache grouped mask-set metadata independently from thumbnail generation.
- [x] Avoid building mask thumbnails during interactive brush strokes.
- [x] Defer mask thumbnail refresh until brush raster commit settles.
- [x] Remove noisy affected-pixel raster mattes from normal mask preview rendering and paint final outlines as a single overlay color.
- [x] Add a small LRU cache for active raster matte preview images by mask state, render state, mode, and preview edge.
- [x] Cache low-resolution scaled mask mattes for thumbnails and active preview paths.
- [x] Skip no-op matte resampling when a preview/thumbnail source is already at the target size.
- [x] Prove brush-stroke mask preview edits do not request full-resolution rendering for overlay-only updates.
- [x] Coalesce queued brush strokes for the same mask/settings into one raster worker job.
- [x] Preserve separate brush stroke history entries while rasterizing coalesced queued strokes together.
- [x] Cancel and replay stale in-flight brush raster jobs when a newer compatible stroke supersedes them.
- [x] Skip source image decode for source-free manual brush commits when a raster matte already exists.
- [x] Keep smart-selection hover previews source-free in normal overlay, edge, affected, and marching-ants modes.
- [x] Draw clean vector fill/outline overlays instead of noisy smart-selection raster preview PNGs in normal overlay mode.
- [x] Cache smart-selection hover outline extraction separately from committed mask and preview-source caches.
- [x] Prewarm smart selection instance maps on hover only after the pointer pauses, with jitter filtering and duplicate request suppression.
- [x] Bound auto-mask brush commit source decodes by stroke footprint so small strokes use a smaller working image than the old fixed 1024-edge decode.
- [x] Use a lightweight active-outline cache key for clean overlay modes so unrelated crop/global adjustment state and the full mask stack do not invalidate brush/object outlines.
- [x] Skip auto-mask edge-barrier evaluation for pixels already rejected by the brush color tolerance gate.
- [x] Add a smoke assertion that normal overlay mode keeps `activeMaskPreviewSource` empty while the canvas overlay remains visible.
- [x] Generate mask thumbnails from the low-resolution matte cache instead of rebuilding local masks.
- [x] Skip full-resolution preview scheduling for brush strokes that only need an interactive overlay update.
- [x] Merge pending brush commits targeting the same mask before launching background rasterization.
- [x] Make smart-selection hover previews use clean vector fill/outline rendering in normal overlay mode.
- [x] Keep active clean-outline cache hits across unrelated global adjustment changes.

## Next Candidates

- [x] Add a smoke assertion that normal overlay mode keeps `activeMaskPreviewSource` empty while the canvas overlay remains visible.
- [x] Add a maximum outline point budget for generated/raster masks and simplify paths before exposing them to QML.
- [x] Split brush cursor repaint from mask overlay repaint so cursor movement does not repaint static mask geometry.
- [x] Avoid clearing and redrawing full overlay canvases when only the brush cursor moved.
- [x] Cache grouped mask-set metadata across QML frames and invalidate only on mask stack revision changes.
- [x] Avoid building mask thumbnails during interactive brush strokes.
- [x] Defer mask thumbnail refresh until stroke commit settles.
- [x] Generate mask thumbnails from the low-resolution matte cache instead of rebuilding local masks.
- [x] Add a small LRU cache for active raster matte preview images by mask id, revision, mode, and preview edge.
- [x] Skip full-resolution preview scheduling for brush strokes that only need an interactive overlay update.
- [x] Increase brush stroke coalescing so multiple short segments commit as one background job.
- [x] Merge pending brush commits targeting the same mask before launching background rasterization.
- [x] Cancel stale brush raster jobs when a newer stroke supersedes them.
- [x] Skip source image decode for source-free manual brush commits with existing matte dimensions.
- [x] Make smart-selection hover previews use clean vector fill/outline rendering in normal overlay mode.
- [x] Cache smart selection hover outlines separately from committed mask mattes.
- [x] Keep active clean-outline cache hits across unrelated global adjustment changes.
- [ ] Reuse brush raster tile caches across adjacent strokes with the same source image size.
- [x] Bound auto-mask source decode size by brush radius and dirty stroke footprint instead of a fixed preview edge.
- [ ] Use a cropped source patch for auto-mask brush preview instead of decoding/rasterizing the whole preview image.
- [x] Skip auto-mask edge checks for color-rejected pixels before computing barrier statistics.
- [ ] Cache auto-mask anchor color/edge statistics per stroke.
- [x] Prewarm smart selection instance maps on hover only when the pointer pauses.
- [ ] Use a lower-resolution foreground instance map while dragging, then refine on click/commit.
- [ ] Avoid rebuilding the full mask stack when toggling preview-only refinement modes.
- [ ] Use incremental mask composition when only the active source changed.
- [ ] Skip local adjustment recomputation while only the selection overlay is changing.
- [ ] Use mask dirty bounds to limit inpaint preview source extraction.
- [ ] Cache source image decodes used only for mask preview/refinement by file path and preview edge.
- [ ] Add a benchmark fixture for brush stroke latency using a real 24MP RAW-backed asset.
- [ ] Add performance gates for `preview_matte_elapsed_ms`, `live_stroke_elapsed_ms`, and visible overlay queue wait.
- [ ] Add observability summaries that report p50/p95 brush commit latency by mask mode.
- [x] Add a one-command mask performance proof script that runs the masking latency tests and summarizes the telemetry.

## Proof Gate

- `scripts/perf/run-mask-performance-proof.py --build-dir build/dev-macos-debug` reruns the focused masking smoke/controller checks, verifies the 50 implemented checklist items, checks key source contracts, and writes a JSON report under `perf-artifacts/`.
- `scripts/perf/test-mask-performance-proof.py` self-tests the proof runner status logic and required source markers without running the expensive controller suite.
