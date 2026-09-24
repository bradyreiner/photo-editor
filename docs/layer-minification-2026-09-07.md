# Footprint-aware layer minification

Follow-up: [verified retained-layer payload reuse](layer-payload-reuse-2026-09-07.md)
measures and reduces repeated host-side compilation work, preserving output bytes.
The cold-publication figures below describe this earlier checkpoint.

## Problem and correction

The previous alpha-aware bilinear change still sampled only four source pixels
when shrinking a layer. Many source pixels could fall between those samples,
causing aliasing/moiré and phase-dependent brightness on fine patterns.

For scales below one, the CPU legacy and retained reference paths and Metal
retained-layer kernel now widen the separable tent footprint to `1 / scale`.
Filter coefficients are normalized over the full discrete footprint, including
transparent padding. Clipping the normalization to the finite image instead
would inflate edge opacity. Associated RGB/alpha are filtered together before
returning straight RGB to the existing layer compositor. Negative/HDR RGB and
hidden-color exclusion are preserved. At scale one and above, the previous
bilinear path and exact integer samples remain unchanged.

Scaling a filter's support with the reduction ratio is also used in
[Pillow's resampling implementation](https://github.com/python-pillow/Pillow/blob/main/src/libImaging/Resample.c).
This implementation is written for the app's finite transparent layers and
linear float pipeline; it does not copy Pillow's boundary normalization.

The supported minimum scale of 0.05 bounds the radius to 20 source pixels.
The implementation samples this bounded footprint directly without allocating
another source-image pyramid. The rendered-preview cache fingerprint advances
to `rendered-preview-v9-antialiased-layer-minification`. Recipe semantics and
source files are unchanged.

Both checked-in Metal source and the embedded fallback were updated. Growth of
the fallback exposed the compiler's portable string-literal limit; its source
is now assembled from two individually bounded NSString literals.

## Independent quality evidence

The expanded `photo_layer_sampling_test` covers 3,240 cases across four working
spaces, pixel/source/text kinds, nine scales (0.05 through 32), five rotations,
three backdrop alphas, mask/opacity, HDR and hidden transparent colors. The
reference sums source texel contributions independently and accounts for the
whole footprint normalization, not a call into the production helper.

- Before: 648 failures, worst associated-RGB/alpha difference 0.207188.
  `/private/tmp/layer-minify-before-test.log`.
- After CPU and explicit native Metal: zero failures, maximum 5.05121e-7,
  below the unchanged 2e-5 gate.
  `/private/tmp/layer-minify-fixed-test.log` and
  `/private/tmp/layer-minify-native-test.log`.
- An independent analytic checker test covers 3,071 interior pixels at scales
  0.05, 0.1 and 0.25 and rotations 0, 13 and -37 degrees. These even-width tent
  footprints should produce exactly half gray regardless of subpixel phase.
  Old four-tap reconstruction has RMS error 0.13125; the corrected CPU output
  has measured error zero and preserves full interior alpha. This is one
  frequency-pattern gate, not an exhaustive photographic sharpness assessment.

## Real full-resolution source reductions

The existing 4208x3120 Nexus 6 DNG and associated JPEG were decoded as full-size
placed sources, reduced to 12%, rotated 17 degrees, and composited at 0.8 opacity
onto a 697x567 canvas. All four working spaces were tested. The recipe is
serialized/parsed before compilation; independent geometry comes from its
constants rather than compiled uniforms. Preview and 16-bit output use ACES 2
and sRGB output.

Evidence root: `/private/tmp/layer-minify-photo-N5Tk6d/`.

| Input | Maximum independent CPU error | Retained CPU error | Native Metal error | Visible preview/output 8-bit difference |
| --- | --- | --- | --- | --- |
| Full-resolution DNG | 1.120e-7 | 5.853e-5 | 5.192e-5 | 1 |
| Full-resolution JPEG | 5.961e-8 | 5.853e-5 | 5.192e-5 | 1 |

The CPU gate remains 2e-6; float-coordinate retained/Metal gates remain 2e-4.
Errors above are maximum associated RGB/alpha, not perceptual color differences.
Native checks require GPU availability and actual readback, never silent fallback.
Logs: `raw-full.log`, `jpeg-full.log`. Per-profile RGBA16 and composite images
are stored alongside them; both ProPhoto photographic composites were inspected.
The input originals were only read.

The synthetic tagged cutout also passes. It initially exposed an overstrict
preview test: at (87,84), the two outputs had alpha zero with RGB values
`#0024a161` and `#0022a162`. The corrected test still requires one-level agreement
for alpha everywhere and RGB wherever either output has coverage. It reports
undefined zero-alpha RGB separately (maximum two levels) instead of treating it
as a visible defect. The visible one-level threshold was not widened.
Diagnostic: `/private/tmp/layer-minify-parity-diagnostic.log`; final
`cutout.log` under the evidence root. The underlying working-pixel numerical
reference still checks alpha and associated RGB everywhere.

## Runtime and regression checks

- Nine focused tests pass in 8.87 seconds:
  `/private/tmp/layer-minify-final-regressions.log`. These cover sampling,
  photographic sampling, alpha, photographic alpha, layer input color, layer
  LUT parity, canonical working fusion, preview math and general render parity.
- The native `metal_aces2_output_lut_parity_test` passes existing thresholds:
  `/private/tmp/layer-minify-metal-regression.log`.
- The main desktop/engine build passes:
  `/private/tmp/layer-minify-integration-build.log`. The only reported warning
  is the existing `fastMathEnabled` deprecation. The final image-test build is
  warning-free, and scoped whitespace checks pass.
- Normal native tests use the checked-in shader through `load_shader_source`.
  An additional native probe extracts and assembles both embedded literals,
  compiles the Metal library and creates its retained-layer compute pipeline:
  `/private/tmp/layer-minify-embedded-shader-test.log`. This proves fallback
  compilation, not a separate fallback pixel-parity run. Probe source/build
  artifacts use the same `/private/tmp/layer-minify-embedded-shader-*` prefix.

Single diagnostic passes on the full photographic sources recorded CPU layer
stage times of roughly 118–131 ms, retained CPU 101–108 ms, and native publication
plus source upload/readback 134–159 ms. The full multi-profile DNG test used
about 2.45 GB peak footprint; JPEG about 1.89 GB. These include test copies,
references and multiple output paths, not just an interactive render. They are
not controlled before/after speed measurements or foreground latency evidence.

## Remaining work

This removes the fixed-four-sample minification defect, not all aliasing at all
frequencies. A scaled tent has sharpness/stop-band tradeoffs; broader photographic
and frequency-corpus evaluation, higher-order/anisotropic filters and cached or
separable prefiltering remain relevant. Direct footprint sampling has real cost.
The measured cold publication/readback path is not realtime; reuse of GPU source
resources and actual interactive scheduling need separate work and proof.

Full-resolution *sources reduced to preview-sized output* are tested here, not
full-resolution rotated output or an end-to-end UI/stylus session. Only one RAW
camera, ACES 2 and sRGB output were exercised photographically. Independent color
calibration, loading/recovery, embedded inference, temporal 4D, film fidelity and
full professional tool coverage remain open in the active goal.
