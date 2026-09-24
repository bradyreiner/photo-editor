# Live mask sampling consistency — 2026-09-07

## Changes

Settled active outlines and selection/range raster-preview generation now use
`RecipeMaskSamplingContext`, instead of an uncharacterized QImage multiplied by
approximate white-balance and exposure gains. The context retains decoder
metadata, floating-point working-space values, source-scoped RAW highlight
analysis, global scene/tone/curve/look edits, preceding local adjustments and
the original pre-layer edge reference. Group refinement participates in composite
coverage. Refined/original raster comparisons share the prepared color context.

Color-dependent range outlines run on the existing worker even for small images.
Small geometric brush refinements retain their synchronous/immediate path; the
existing direct-interaction regression verifies this boundary.

Outline cache identity now includes a versioned global recipe signature rather
than only exposure/temperature/tint. The signature omits large mask/layer payloads:
mask changes already have a separate work revision, and photo layers follow the
local-mask stage. Completed workers also compare that signature with the current
recipe before publication. Edit intents notify outline bindings, so a contrast
or other global color change cannot silently leave the old range outline active.

Two related presentation defects were corrected:

- Selection/range raster PNGs now store straight RGB in a straight-alpha image.
  The old code wrote straight values into a premultiplied-alpha image, distorting
  the chosen overlay color when encoding it.
- A valid evaluated empty alpha no longer falls back to a geometric outline.
  The clean-geometry shortcut also rejects source/group opacity and density
  changes and group post-composition refinements rather than assuming the
  unrefined shape is final coverage.

## Evidence

`editor_mask_sampling_test` uses the real controller with an isolated injected
document/recipe, public contrast/density/preview controls and its real outline
worker. It does not simulate a complete import or foreground QML session.

For a synthetic Display P3 JPEG and the real 4208×3120 Nexus 6 DNG/JPEG pair:

- Four built-in working RGB spaces, each with/without crop, pass.
- The DNG must exercise camera-linear RAW decoding.
- The `color-on-bw` raster's decoded PNG alpha equals the engine-derived coverage
  with the mode's 0.42 display-opacity factor, exactly. Every nontransparent
  pixel retains the requested `#527396` RGB color exactly.
- The settled outline worker's alpha hash equals the same recipe-aware sampling
  result. This proves underlying raster coverage, not an independent contour
  simplification or photographic-quality reference.
- An outline is launched, contrast changes before completion, and the stale
  worker result is rejected. Each fixture/profile/crop case records a stale
  rejection and receives the new alpha hash.
- Setting mask density to zero produces valid empty alpha and no final outline.
- The shared sampler additionally matches the preexisting engine-stack matrix
  in `mask_thumbnail_color_test`, including disabled/bypassed/soloed predecessors,
  recipe ordering and group refinement. Its independent rendered-pixel edge
  coverage fixture still reports maximum alpha-byte difference 0.

Six focused tests pass together: render executor, engine color pipeline, existing
outline controller, existing layer controller, new live sampling controller and
mask-thumbnail color. Existing range-outline tests now wait for the worker and
check its underlying composite-alpha provenance; their geometry/provider checks
remain. The app builds successfully and scoped whitespace checks pass.
The saved real RAW matte was visually inspected; this is an artifact check,
not foreground application or interaction proof.

Receipts:

```text
/private/tmp/mask-overlay-final-build.log
/private/tmp/mask-overlay-artifact-build.log
/private/tmp/mask-overlay-accepted-tests.log
/private/tmp/mask-overlay-stack-real.log
/private/tmp/mask-overlay-raw-final.log
/private/tmp/mask-overlay-jpeg-final.log
/private/tmp/mask-overlay-raw-overlay.png
/private/tmp/mask-overlay-raw-matte.png
```

Earlier `mask-overlay-build.log`/`build2.log` contain integration compile failures
resolved by keeping crop declarations local to their existing implementations.
`mask-overlay-focused.log`/`focused2.log` contain intermediate async-admission
and stale synchronous-test expectations, not accepted final proof.

## Limits and next work

- The controller proof uses the normal 512-edge mask input, not full-resolution
  mask evaluation. Thumbnail proofs use their own smaller inputs. Cross-resolution
  RAW highlights, full-resolution memory/latency and broader cameras/ICC/DCP
  calibration remain unproven.
- The raster fill getter still decodes, prepares and encodes synchronously on a
  cache miss. Moving that path to cancellable/coalesced background work remains
  necessary for reliable interactive/full-resolution use. No realtime claim.
- Default overlay-mode vector fills, live stroke approximations, all remaining
  geometric/range-refinement shortcuts, range-picker sample interpretation,
  geometry-transform alignment, and all retouch preview variants need separate
  end-to-end coverage. The PNG color assertion specifically covers `color-on-bw`.
- The full app goal remains active: loading, model weights and calibrated agent
  workflows, genuine 4D reconstruction and complete professional editing tools
  are not completed by these mask fixes.
