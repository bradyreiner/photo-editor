# Straight-alpha layer compositing

Follow-up: [layer transform sampling](layer-transform-sampling-2026-09-07.md)
replaces nearest-neighbor sampling with alpha-aware filtering and advances the
preview-cache fingerprint again. Evidence below records this earlier checkpoint.

## Reproduced defects and correction

The engine stores unassociated (straight) RGB. Its layer compositors interpolated
RGB against the background as though that background were opaque, then updated
alpha separately. A foreground over a transparent background therefore acquired
darkened/contaminated stored RGB. Non-normal adjustment layers also increased
coverage: a background alpha of 0.25 became 0.625 at half layer opacity.

The initial independent 1,024-case CPU regression failed 408 cases, with a worst
channel difference of 0.68. Baseline: `/private/tmp/layer-alpha-before-test.log`.

The legacy CPU and compiled CPU layer paths now share a straight-alpha source-over
helper. Both the checked-in Metal shader and its runtime-compiled source implement
the same correction. Only the overlapping source/background region uses the blend
function; source-over combines associated contributions, then divides by output
alpha. This follows the [W3C general compositing formula](https://www.w3.org/TR/compositing-1/#generalformula).
An entirely transparent backdrop contributes no hidden RGB, including for blend
modes. The opaque-backdrop branch preserves its established arithmetic. RGB is
not clamped by the new helper, preserving scene-linear negative/HDR values.
Adjustment layers change RGB but preserve existing alpha in all three paths.

The rendered-preview cache fingerprint advances to
`rendered-preview-v7-straight-layer-alpha` to invalidate old composited pixels.
No recipe schema or UI controls changed.

## Verification

- `photo_layer_alpha_test`: 4,096 independent source-over cases across Linear
  sRGB, Display P3, Rec.2020 and ProPhoto; fill/pixel kinds; normal/multiply;
  transparent/partial/opaque backgrounds; source alpha, layer opacity and mask
  combinations. CPU stage and compiled reference pass with maximum error
  5.96046e-8 against a separately expressed equation (gate 2e-6).
- Additional tests cover all 14 blend modes over hidden HDR background RGB at
  zero alpha, normal-mode negative/HDR preservation, and adjustment-alpha
  preservation across five blend modes. These are additional to the 4,096 count.
- Explicit `--metal` runs require an available native backend, publish working
  float surfaces, and compare actual GPU readback; no CPU fallback is accepted.
  `/private/tmp/layer-alpha-final-native-test.log` passes.
- The broader `metal_aces2_output_lut_parity_test` passes unchanged tolerances:
  `/private/tmp/layer-alpha-metal-pipeline-regression.log`.
- Seven CPU/integration regressions pass in 4.88 seconds:
  `/private/tmp/layer-alpha-final-regressions.log`. Targets are
  `photo_layer_alpha_test`, `photo_layer_alpha_image_test`,
  `photo_layer_color_input_test`, `layer_lut_render_parity_test`,
  `canonical_native_working_fusion_test`, `editor_preview_math_test`, and
  `render_parity_test`. The final checker-background assertion also passes in
  `/private/tmp/layer-alpha-checker-fixture.log`.
- The desktop/engine build completed; scoped whitespace checks pass. This is
  not a clean-tree assertion or a passing claim for the full controller suite.

## Photographic integration

`photo_layer_alpha_image_test` places an actual linked photo on a transparent
float canvas, round-trips the mask/opacity recipe, and renders 16-bit output and
8-bit preview. At every nonzero coverage pixel it independently requires the
same straight RGB as the fully opaque placed photo. Mask coverage and output
alpha are checked separately against the explicit mask/opacity formula.

Inputs are the existing Nexus 6 DNG and associated JPEG under
`/private/tmp/framehouse-six-raw-source-v3/hdrplus-nexus6-skin-0218/`; originals
are read-only. Tagged synthetic Display-P3 input is also covered.

Evidence root: `/private/tmp/layer-alpha-photo-Dh6U6V/`.

| Input and size | Working/output coverage | Result |
| --- | --- | --- |
| DNG, 800x593 and 1600x1186 | Four working profiles, legacy and ACES 2, sRGB output | All 16 cases pass |
| JPEG, 800x593 and 1600x1186 | Same | All 16 cases pass |
| DNG, 4208x3120 | Linear ProPhoto, ACES 2, sRGB output | Pass |
| JPEG, 4208x3120 | Linear ProPhoto, ACES 2, sRGB output | Pass |

Logs: `raw.log`, `jpeg.log`, `raw-full.log`, `jpeg-full.log`. Every case has zero
16-bit RGB/alpha error and at most one 8-bit level of preview/output difference.
The test writes `masked-photo.png` and `checker-proof.png` into new output
directories and asserts the zero-mask area preserves alternating background
pixels. The full-resolution JPEG checker composite was visually inspected.

## Boundaries

This fixes layer alpha compositing, not all color edits or Photoshop compatibility.
Normal/multiply partial-alpha cases have an independent numeric reference;
the other blend modes have transparent-backdrop invariants and adjustment-alpha
checks, not independent certification of their existing blend equations.
Existing small-alpha early-out thresholds and mask-density semantics are unchanged.
The photographic color reference shares the app's source conversion/output
engine, so this is color consistency rather than independent colorimetry.

Only one RAW camera and sRGB output are exercised photographically here. Full
resolution is tested in ProPhoto/ACES 2 only. GPU proof uses numeric surfaces;
real-photo integration uses CPU rendering. There is no new foreground interaction,
stylus, undo/relaunch, transparent-source interpolation, or realtime measurement.
Broader working/output spaces, camera calibration, professional tool coverage,
loading, actual embedded inference, 4D and film fidelity remain open in the active
completion goal.
