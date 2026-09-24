# Crop highlight consistency through canonical previews

## Changes

The canonical preview Decode callback now carries source-bound RAW highlight
analysis into the render surface, matching the contract added to flattened output
in `crop-zoom-and-raw-highlight-export-2026-09-07.md`. Analysis occurs on the full
decoded field of view before cropping, rather than on only the retained pixels.
The RAW prefix reuses this analysis instead of recomputing scene chrominance from
the crop. It respects the selected recovery mode and engine-owned camera-linear
decode boundary. The regular decode path already materialized the full linear
source before cropping, so the new analysis uses that existing buffer.

This also reaches adjustment thumbnails using the shared preview adapter, and
the tested tiled canonical publication path. An overlay request can obtain
source analysis separately, but the existing high-resolution region-overlay
decoder excludes RAW files; no RAW region-overlay capability is claimed here.

The tests exposed another correctness defect: adjusted, rendered and tile cache
keys did not distinguish draft/settled or full/interactive requests when their
decoded sizes matched. A saturated gradient reproduced 6,223 different retained
pixels, at up to two RGB8 levels, even after explicitly requesting a full render.
All three cache-key families now include both quality flags. The corrected test
passes exact equality, without a pixel tolerance. Rendered-image, adjusted-image,
tile and persisted JPEG preview version identifiers also change so previous
outputs cannot masquerade as outputs of the new renderer.

## Verification contract

`editor_crop_zoom_test` now always exercises these paths, including its default
generated JPEG used in CTest. Real photographic fixtures can be supplied as its
only argument. The controller uses a lossless image sink. The test starts an
explicit full-resolution, non-draft render with in-memory publication, so it
compares final pixels rather than an intermediate draft or the JPEG carrier.
This is a real controller/worker/publication test, not a replacement renderer.

The test requires:

- Exact retained RGB pixels between full and cropped controller previews.
- Exact RGBA pixels through the shared adjustment-thumbnail adapter at 512 and
  1600 source edges, each compared at its own decoded resolution.
- A fresh tiled render with positive executed-tile count and exact crop pixels.
  Dedicated request cache identities prevent the prior complete preview from
  satisfying the tile assertion without executing tiles.
- Exact full-resolution preview/flattened-output equality before and after crop.
- The existing exact crop-output, return-to-full-frame, undo/redo, repeated zoom,
  off-center geometry and zoom-limit checks.

Initial diagnostic attempts are not acceptance evidence. Two early requests
captured a 256-pixel draft, and the file-backed preview comparison included JPEG
compression differences. The lossless sink and explicit full-resolution request
correct those test boundaries. The subsequent small-gradient failure at
`/private/tmp/crop-preview-final-carrier.log` was a real cache-quality defect;
after quality isolation it passes at
`/private/tmp/crop-preview-quality-cache-check.log`.

Final photographic receipts pass:

- `/private/tmp/crop-source-final-raw.log`: real Nexus 6 DNG, 4208 x 3120 full
  frame and 2104 x 1560 crop. Zero retained RGB differences in the lossless
  preview; exact preview/flattened-output equality for both full and cropped
  frames; both shared-thumbnail sizes pass; 20 fresh tiles actually execute and
  match the reference crop exactly.
- `/private/tmp/crop-source-final-jpeg.log`: the matching reference JPEG passes
  the same dimensions, exact pixel comparisons, two thumbnail sizes, 20-tile
  assertion, and existing history/geometry checks.

The final RAW preview image at `/private/tmp/crop-source-final-raw-preview.png`
was visually inspected. Fixtures are read-only; app data, cache and persistence
configuration belong to temporary test roots. No user catalog, model weights or
original image is changed. These are controller and worker runtime proofs with
lossless publication, not a live packaged-app pointer/gesture performance run.

The final app/controller/layer-regression build is
`/private/tmp/crop-preview-quality-cache-build.log`; it contains no compiler
warnings or errors. Scoped `git diff --check` passes.

Four focused tests pass in 27.60 seconds at
`/private/tmp/crop-source-final-tests.log`: render executor, editor layer
adjustments, source-layer thumbnails and the expanded crop regression. The
earlier run at `/private/tmp/crop-preview-focused-tests.log` was 3/4 because the
new gradient test caught quality-cache contamination; it is not substituted for
this final run.

## Remaining limits

Source-bound means the complete field of view at the currently decoded
resolution, not necessarily native sensor resolution during a small preview.
Crop invariance at each tested resolution does not prove draft/full-resolution
color parity. The analysis still scans the full decoded source on the worker;
reuse, cancellation latency and foreground input-to-photon performance need
separate measurement and optimization.

These changes cover the canonical preview callback and the shared adjustment
thumbnail path, not every legacy renderer, catalog thumbnail, mask-preview
thumbnail, export route or spatial filter. The RAW region-overlay decoder remains
unsupported. Broader mask/brush dirty-tile behavior, DCP spatial maps, camera
corpora, HDR/output spaces, and fully interactive packaged-app proof remain open.
The full RAW/loading/3D/4D/model/Photoshop-class editing goal is not complete.
