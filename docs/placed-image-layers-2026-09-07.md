# Placed image layers and working-space color

Follow-up: [straight-alpha layer compositing](layer-straight-alpha-2026-09-07.md)
corrects translucent-background RGB and adjustment alpha, with CPU/Metal and
real full-resolution RAW/JPEG regression evidence.

## Defects reproduced

The Source / Image Layer menu entry was disabled and had no file-picker action.
The internal source-layer compiler loaded files with `QImage(path)`, bypassing
the RAW decoder/characterization contract and image orientation handling. Raster
layers always converted into default Linear ProPhoto RGB regardless of the
document's selected working space; fill colors were encoded picker RGB numbers
inserted directly into linear working pixels.

The initial compiler regression measured maximum channel differences of 0.16 for
placed/pixel layers in Linear sRGB and 0.26–0.42 for the fill across working
spaces. Source/pixel layers happened to match only in default Linear ProPhoto.
Baseline: `/private/tmp/layer-color-before.log`.

## Changes

- Source / Image Layer now opens a local file picker for JPG, RAW and other
  supported images. Accepted files become selected linked layers in the Layers
  inspector. File URLs are resolved locally; directories, unreadable and
  unsupported files are rejected before undo/state mutation.
- Source dimensions use the RAW sensor/decode size, not an embedded JPEG size;
  raster orientation is respected. New source layers store their original
  document canvas dimensions so preview rasters and positions scale together.
  Original-size layer export remains independent of preview size. Legacy layer
  recipes without canvas dimensions retain their previous coordinate convention.
- Fill, pixel, text and placed-image inputs convert to the selected working
  profile before compositing. Placed RAW data uses its own canonical normalization
  and camera-characterization prefix, without inheriting the base photo's camera
  metadata or baking a display/output transform into the layer. Layer source
  scaling happens in linear float32; float32 input is retained into compositing.
- Canonical v13 adaptation recognizes a linked source layer as raster content.
  The existing derived-content binding path supplies its plan identity.
- The rendered-preview disk-cache version advances so old color interpretations
  cannot be reused after this change.

## Verification and corrections

The regression covers all four supported working profiles (Linear sRGB, Linear
Display P3, Linear Rec.2020 and Linear ProPhoto), fill/pixel/source compilation,
tagged Display-P3 JPG input, recipe encode/decode, layer placement versus the
same image as a base, ACES 2 canonical admission, legacy and ACES 2 sRGB output,
and 8/16-bit preview/export agreement. The ordinary-image and layer references
share the app's conversion engine; this is consistency proof, not independent
colorimetric certification. Preview placement coordinate scaling is also checked.

Three focused tests passed in 1.83 seconds: `photo_layer_color_input_test`,
`layer_lut_render_parity_test`, `edit_recipe_v13_adapter_test`. Log:
`/private/tmp/layer-color-final-regressions.log`.
The broader `editor_preview_math_test` and `render_parity_test` also passed in
4.25 seconds: `/private/tmp/layer-color-broad-regressions.log`. This does not
establish every layer/backend/input/output combination.

The existing 4208x3120 DNG fixture, rendered both as an ordinary RAW image and as
a linked layer over a JPG-space canvas, matches with **zero RGB8 differences**
at full resolution. Log: `/private/tmp/layer-color-raw-verified.log`. An earlier
attempt differed by five levels because of an extra half-float layer
intermediate; the float32 correction passed without widening the two-level gate.
This real-RAW comparison currently covers one camera and legacy output.

The first native attempt exposed a missing canonical source-kind mapping and
the initial proof incorrectly accepted a retained previous preview. Its receipt
at `/private/tmp/framehouse-placed-layer-ZWg7l4/` is **not accepted evidence**.
The corrected proof requires no preview failure and a source different from the
pre-placement preview, and the compiler regression now checks canonical admission.
Legacy recipes intentionally cannot be admitted as v13; the canonical assertion
applies to ACES 2, while legacy render parity is tested separately.

The corrected native run at `/private/tmp/framehouse-placed-layer-final-q4DluJ/`
passed QML placement, actual layer rendering, standalone PNG layer export and
undo/redo using a real JPG base and DNG layer. Generation 3's full-resolution
render completed the canonical layers stage in 1,568 ms total (CPU). This is not
realtime performance. The 18,821 ms overall proof includes isolated catalog
timeouts and test capture work. Its private model runtime stopped and port 59085
was independently closed. Its screenshot still had the first-run guide covering
the Layers inspector. A subsequent run at
`/private/tmp/framehouse-placed-layer-visible-G2gbcn/` used the guide's normal Skip
action and visibly showed the RAW image and selected layer; port 59602 was closed.
That check exposed an incorrect Base Image label on source layers. It is now
Source Image, with a native label assertion. Final proof:
`/private/tmp/framehouse-source-layer-verified-HonCnZ/`. It passed in 16,826 ms,
reports placement, fresh preview, layer export, undo/redo, the Source Image label
and the normal first-run Skip action. Its screenshot was visually inspected:
the RAW photo, selected source layer and layer controls are unobscured. Its
private runtime stopped and port 59908 was independently confirmed refused.
The earlier first-run-overlay screenshots are retained as intermediate evidence.

Final native/app builds passed with no compiler warnings/errors found in the
successful logs: `/private/tmp/layer-color-final-build.log`,
`/private/tmp/layer-proof-final-build.log`,
`/private/tmp/layer-color-broad-build.log`, and
`/private/tmp/layer-label-final-build.log`. `git diff --check` passes. Existing
runtime QML warnings and the unavailable-catalog shutdown warning remain; build
success is not a claim of warning-free runtime or healthy persistence.

The native proof invokes the QML accepted-file handler; it does not automate
navigation inside the operating system's native file picker. It uses temporary
catalog/cache/preferences and existing read-only fixtures, with no model weights
downloaded. The deliberately unavailable catalog produced a pending
`save_edit_recipe` process warning at shutdown: undo/redo and recipe serialization
passed, but this is not a healthy-catalog save/relaunch proof.

## Remaining boundaries

RAW layer thumbnails still use the older generic raster thumbnail path; they
are not yet canonical color proofs. Source files remain external linked assets:
missing/corrupt-source failure containment, filesystem-revision cache
invalidation and restoration are now covered by
`linked-image-source-reliability-2026-09-07.md`. Relinking and automatic watching
are verified in `source-layer-relink-and-refresh-2026-09-07.md`. Immutable content
snapshots and self-contained packaging remain open. Per-source RAW
develop controls, mixed-source HDR/color corpus coverage, transformed/tiled/crop
edge cases, foreground manipulation latency and GPU parity need further proof.
Repeated source decode/characterization is not yet a retained GPU layer pipeline.
This is not completion of Photoshop-class layers or the full active app goal.
