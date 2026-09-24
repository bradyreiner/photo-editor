# Crop zoom and RAW highlight consistency in flattened output

## Fixed behavior

The first crop zoom on an implicit, uncropped document applied the zoom twice.
`setCropZoom` updated `crop_zoom_` before resolving the current rectangle, so
implicit geometry already used the new zoom before being scaled by old/new zoom.
At 2x, it selected 25% of each dimension instead of 50%. The controller now
resolves the old rectangle first. Explicit rectangles retain the existing
center-preserving, image-bounded zoom behavior.

Full-resolution verification exposed another defect in the file-backed flattened
output renderer. RAW highlight reconstruction estimated scene chrominance after
the source was cropped. Changing crop therefore changed the color of retained
highlights. In the real Nexus 6 DNG, 324 retained pixels changed, with a maximum
14-level RGB8 channel difference, despite no tone/color edit.

For cropped camera-linear RAW with opposed highlight recovery enabled, the
flattened-output Decode callback now computes its reconstruction analysis from
the complete source. It attaches that analysis to the cropped render surface via
the existing `highlight_reconstruction` contract. The executor carries the
analysis into normalization/characterization instead of recalculating it from
the crop. The reconstruction algorithm, recipe schema and user settings are not
changed. Whole-frame output is unchanged; raster inputs and disabled recovery
do not take this additional full-source analysis path.

## Evidence

`editor_crop_zoom_test` uses the actual controller in private runtime/cache
directories, with catalog persistence explicitly disabled. It creates a real
gradient JPEG by default and accepts a read-only photographic fixture path.
It tests:

- First 2x crop is centered and half-size in each dimension.
- Published editor preview changes, undo/redo restores exact recipe state,
  returning to 1x restores the full rectangle, and repeated zoom scales once.
- Off-center rectangles preserve their center where possible; zoom bounds and
  image-edge constraints remain enforced.
- The file-backed flattened output is non-null and equals an independently
  selected central rectangle of the full-frame output, with **exact** pixel
  equality. The oracle does not call the crop-geometry helpers. Returning to
  full frame reproduces the complete original output exactly.

The initial regression failed as intended at
`/private/tmp/editor-crop-zoom-before.log`: first 2x zoom produced 0.25 x 0.25.
An intermediate pixel test used the color-only image API, which does not perform
the caller-owned crop; that diagnostic is not acceptance evidence. The test was
corrected to call the actual file-backed flattened-output renderer. Its RAW
failure is `/private/tmp/editor-crop-zoom-raw-diagnostic.log`; the 324-pixel,
14-level discrepancy was not accepted with a tolerance or hidden by disabling
highlight recovery.

Accepted full-resolution photographic runs:

- Real DNG: `/private/tmp/editor-crop-zoom-raw-fixed.log`.
- Matching reference JPEG: `/private/tmp/editor-crop-zoom-reference-jpeg.log`.

Both render a 4208 x 3120 full image and a 2104 x 1560 crop, pass exact central
pixel/return-to-full-frame comparisons, and pass controller/history/zoom-bound
checks. The DNG cropped output and actual published editor preview were visually
inspected at `/private/tmp/editor-crop-raw-fixed-crop.png` and
`/private/tmp/editor-crop-raw-fixed-preview.png`. These are rendered-image and
controller-runtime checks, not a packaged-app pointer/gesture interaction proof.
No original fixture or user catalog was modified.

App and crop-test builds pass in `/private/tmp/editor-crop-highlight-build.log`;
related regression relinking is `/private/tmp/editor-crop-regression-build.log`.
Neither log contains compiler warnings/errors. Scoped `git diff --check` passes.

The focused CTest run at `/private/tmp/editor-crop-final-tests.log` is **2/3**, not
all green: `editor_crop_zoom_test` and `edit_recipe_v13_persistence_test` pass.
`desktop_shell_crop_test` stops in the existing shared source-contract checks at
`tests/desktop_shell_smoke/source_contract.inc` (the shared menu/split-button
keyboard, disabled-reason and geometry contract), before crop interactions run.
Those controls were not changed in this slice. The run also emits host audio
device warnings. No packaged crop-interaction acceptance is inferred from it.

## Remaining work

The initial full-source highlight fix was in the file-backed flattened-output
Decode path. The subsequent `crop-highlight-preview-consistency-2026-09-07.md`
records canonical preview, shared adjustment-thumbnail and tiled publication
propagation, plus exact full-resolution preview/flattened-output comparisons.
Other preview/thumbnail/export routes still need coverage; a visibly correct
preview image alone is not evidence of this parity.
Legacy rendering, DCP spatial maps, spatial filters, rotated/perspective crops,
HDR/output spaces and diverse cameras need their own crop-invariance corpus.

Full-source analysis temporarily materializes an additional full-frame linear
buffer for cropped RAW output. Its timing and peak memory have not been accepted
as realtime. Reusing source-bound analysis without stale decode/settings evidence
is follow-up work; this correctness fix does not claim a performance improvement.

Canonical persistence still needs an explicit crop-intent audit: implicit
preset/zoom is not fully represented by canonical rectangle fields, and crop
preset labels are not restored. This change does not conceal that by storing
render-affecting crop behavior only in non-render provenance. The full objective
and all remaining RAW/loading/4D/model/editing gates remain active.
