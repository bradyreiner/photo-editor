# Mask coverage edits: visible feedback and quality restoration

## Defect and change

Opacity, density, inversion, shape/range and refinement setters submitted
preview-only mask rasterization. The coalesced slider transaction kept
schedule_full_resolution=false at release. That draft also stripped local
adjustments and photo layers, so completing a gesture could leave an incomplete
photograph instead of restoring the edit at full quality.

The isolated native opacity probe reproduced this before the fix:
/private/tmp/mask-opacity-before.log, exit 1 (no settled full-generation output).

Forty explicit coverage operations now use mask_rasterization / mask_commit
with full-resolution scheduling. They remain coalesced during the gesture;
release requests current full work. Drafts for mask_commit retain local masks,
retouch and photo layers. Existing detail/lens draft reductions remain. This also
keeps committed smart-selection drafts from dropping those effects.

Color operations retain their separate local_adjustments route. Brush cursor
settings, metadata and per-dab brush paths are not reclassified by this patch.
Other mask-stack mutations and retouch controls still require an inventory and
runtime proof; this is not certification of every editing tool.

## Proof contract

The real-controller native probe now supports --local-opacity and
--local-expansion, plus --local-crop and temporary --local-output PNGs. It runs
the QML slider's begin/set/end coalesced lifecycle, waits for a newer native
texture and full-generation completion, and requires full source/crop dimensions.
Actual Metal readback must change between settings without changing the protected
rightmost quarter. Final flattened PNG export must agree with the native preview
within 2/255 per RGBA channel after common 8-bit conversion. This tests the two
application output paths, not an independent colorimetric reference.

The synthetic 16-pixel half-mask uses expansion amounts 5, 10 and 15, yielding
dilation radii 1, 2 and 3. The original test used 10, 20 and 30 and correctly
failed its protected-region assertion because dilation reached that region.
The corrected fixture preserves distinct radii and leaves the production
morphology algorithm unchanged.

Policy checks cover all forty operation names and negative cursor/metadata/color
cases. The five focused policy/native tests passed in 27.58 seconds in
/private/tmp/mask-coverage-parity-tests.log. Final regression execution passed ten
relevant tests (compiled local adjustment, mask cache, policy, preview math, four
native interactions, legacy and ACES2 references). The eleventh, broad controller
test, still fails at the retry fixture's missing last-good preview, as it did
before this patch. Its changed rapid-coverage contract compiles but is not reached
at runtime. No retry assertions were weakened. Final log:
/private/tmp/mask-coverage-final-regressions.log, 37.22 seconds.

## Real photographic evidence

- RAW opacity: /private/tmp/mask-coverage-real-proof/
  20260908T082724.041540Z-perf-interaction-workflow-59df0581. Three native edits
  settle at 4208x3120 with unchanged protected pixels; final native/export
  maximum channel difference is 1/255.
- Cropped RAW expansion: /private/tmp/mask-coverage-real-proof/
  20260908T082912.074799Z-perf-interaction-workflow-b1169d5e. Three native edits
  settle at 3366x2496, preserve the protected region and agree with final export
  within 1/255. The final native PNG was visually inspected; existing RAW noise
  remains, and no RAW-noise or independent color-fidelity claim is made.
- JPEG opacity: /private/tmp/mask-coverage-real-proof/
  20260908T083012.307702Z-perf-interaction-workflow-2934b31a. Three native edits
  pass at 4208x3120 with protected pixels unchanged and maximum export difference
  1/255.
- Cropped JPEG expansion: /private/tmp/mask-coverage-real-proof/
  20260908T083230.607751Z-perf-interaction-workflow-3a769879. Three native edits
  pass at 3366x2496 with protected pixels unchanged. The final native image was
  visually inspected; flattened-export parity passes the 2/255 bound.

The rebuilt app's isolated RAW-base/JPEG-layer smoke passed (QML placement,
native preview, undo/redo, export). Its receipt and visually inspected screenshot
are in /private/tmp/mask-coverage-app-proof-VdZcbu. The catalog-recovery banner is
expected because this proof deliberately uses a closed catalog port; the user's
catalog is not changed. This checks app regression, not direct mask-slider
input-to-photon behavior.

These runs may overlap compilation/app smoke work. Their timing is functional
telemetry, not a controlled latency comparison. Correct rendering is restored;
realtime performance is not established by this work.
