# Asynchronous active-mask raster previews

Scope: remove expensive raster-inspection work from the active-mask QML getter
without changing the recipe-aware RAW/JPEG sampling established in
`live-mask-sampling-consistency-2026-09-07.md`.

## Implementation

- `ActiveMaskPreviewWorker` admits one immutable request and retains at most one
  newest pending request. Decode, color preparation, mask normalization/scaling,
  rasterization and PNG encoding run on the global worker pool.
- The getter hashes compact source/global-color/revision/view identity, not
  embedded segmentation bytes or complete mask/layer recipes. Qt implicit
  sharing carries the recipe snapshot into the job.
- Exact cached results return immediately. An uncached request returns empty;
  it never presents another mask or recipe's fill as current. Normal outlines
  and direct brush feedback remain separate.
- Publication checks source revision, asset/path, recipe color, mask revision,
  active mask, inspection mode/color, original/refined choice and resolution.
  A cached-state return also cancels unwanted work. Recipe restore/undo/redo
  now advances mask work revision and cancels the old raster request.
- Completed-but-undelivered futures retain their admission slot. Worker
  callbacks deliver on the owning UI thread; destruction disconnects delivery
  without waiting for decoder/encoder completion. Render closures do not
  capture the controller. Cancellation is cooperative at stage boundaries.
- Failed requests are suppressed for their current identity, preventing an
  empty-result notification from starting an endless render loop. A changed
  identity permits retry. This does not add a new user-facing failure panel.
- Existing refinement job identity/stage and bounded preview LRU contracts are
  retained, including cache-hit job attribution. Functional controller tests
  wait for settled output; dedicated tests separately enforce async admission.

## Verification

Final desktop app and relevant targets built successfully, with no warning or
error lines in `/private/tmp/mask-async-final-validation-build.log`.

Eight focused regressions passed in 34.07 seconds:
`render_executor_test`, `color_pipeline_test`, `mask_worker_coordinator_test`,
`editor_preview_outline_trace_test`, `editor_preview_layer_adjustment_test`,
`active_mask_preview_worker_test`, `editor_mask_sampling_test`, and
`mask_thumbnail_color_test`. Receipt: `/private/tmp/mask-async-final-tests.log`.

Additional terminal-success runs:

- `/private/tmp/mask-async-final-raw.log`: synthetic P3 JPEG plus actual
  camera-linear Nexus 6 DNG, four working RGB spaces (sRGB, Display P3, Rec.2020,
  ProPhoto), cropped/uncropped. Exact raster alpha/color and settled outline
  alpha hash, stale global-color rejection, zero-density outline, and same-ID
  recipe-restore rejection all pass.
- `/private/tmp/mask-async-final-jpeg.log`: equivalent actual JPEG coverage.
  Both controller runs also pass seven raster inspection modes, signal-driven
  publication without getter polling, original/refined cache return, and
  immediate source-free overlay mode.
- The 16 MiB segmentation-payload getter returned pending in 0.258 ms in the
  RAW run and 0.300 ms in the JPEG run. These are admission measurements, not
  decode completion or frame-rate measurements. Numerical-reference setup
  decoded sources before the timed getter; cold source loading is not proved.
- `/private/tmp/mask-async-final-thumbnail.log`: real RAW/JPEG working-space,
  crop, stack-prefix and group-refinement comparisons still agree exactly.
- Worker tests exercise 100 edits collapsing into one newest replacement,
  unwanted completion after a cache return, UI-thread publication,
  completed-but-undelivered admission, nonblocking destruction (0 ms in the
  direct run), no late callback, and failure-loop suppression/retry on changed
  identity.
- `/private/tmp/mask-async-raw-matte.png` was visually inspected; the associated
  overlay is `/private/tmp/mask-async-raw-overlay.png`. This is rendered-mask
  artifact inspection, not a foreground native viewport/stylus session.

The broader `editor_preview_controller_test` still exits at its earlier
`model and color diagnostics should report the actual runtime processors`
assertion, before reaching its masking section
(`/private/tmp/mask-async-final-controller.log`). Its synchronous preview
assertions were adapted to settled output, but that whole test is not claimed
passing. The focused tests above independently exercise the changed paths.
Subsequent diagnostics/scope work in `color-scope-contracts-2026-09-07.md`
corrects that earlier assertion and records the next broad-test failure; the
receipt above remains the result of this async-mask verification run.
An earlier test sweep also ran the old sampling binary before its rebuild
completed and failed on the old synchronous expectation; it is not an accepted
receipt. Final tests ran after the build reached terminal success.

The app packaging check verified 58 bundled Ollama runtime files, version
0.33.3, and explicitly reported `modelsInstalled: false`. Runtime packaging is
not model/search readiness. Scoped `git diff --check` passed.

## Limits

This does not prove foreground frame rate, stylus feel, full-resolution memory
bounds, or independent photographic accuracy. Large decode/raster/encode stages
can continue until their next cancellation boundary; the global pool can be
contended by other app work. Outline admission, color-tool overlays, vector
fills and other synchronous editor paths need their own profiling and parity
checks. The broader RAW/loading, Gaussian 3D/4D, model/search/agent and complete
professional-tool goals remain open in `active-app-completion-goal.md`.
