# Color Engine Mathematical Test Plan

> Supporting evidence specification. This document defines focused mathematical proof but does not
> supersede the execution, deletion, migration, or release gates in
> `docs/color-engine-and-app-foundations-completion-strategy.md`.

This plan defines the proof obligations for the color and detail pipeline. The
tests must run against scene-referred floating-point pixels before UI preview,
file export, or `QImage` conversion can hide clipping and hue errors.

## Core Model

Let a pixel be `p = (R, G, B, A)` in linear working RGB. For Linear sRGB,
luminance is:

```text
Y(p) = 0.2126 R + 0.7152 G + 0.0722 B
```

An SDR recipe with `output_headroom = H` is destination-safe only when:

```text
0 <= R' <= H, 0 <= G' <= H, 0 <= B' <= H
max(R', G', B') <= H
```

For a tone operator that preserves chroma by scaling the pixel from old
luminance `Y` to new luminance `Y'`:

```text
p' = p * (Y' / Y)
```

the necessary channel-safety bound is:

```text
Y' <= H * Y / max(R, G, B)
```

This is the current peak failure mode. A luminance-only shoulder can keep
`Y' <= H` while still producing `max(R', G', B') > H` on saturated colors.

## Universal Invariants

- **Finite output:** every channel must satisfy `isfinite(channel)`.
- **Alpha preservation:** global color and detail tools must preserve `A`.
- **Destination headroom safety:** SDR output recipes must satisfy
  `0 <= RGB <= output_headroom` after each family that can alter tone or chroma.
- **Neutral preservation:** if `R = G = B`, tone-only, luma-curve-only, and
  detail-only recipes must keep `R' = G' = B'`.
- **Neutral monotonicity:** for a neutral ramp `x_i <= x_j`,
  `Y(f(x_i)) <= Y(f(x_j))`.
- **Hue continuity:** small input hue changes must produce bounded output hue
  changes; no discontinuity may come from negative-channel clamping.
- **Bounded chroma:** saturation, vibrance, grading, LUTs, sharpening, and
  mixers must not create destination-gamut excursions.
- **Identity preservation:** disabled tools and identity LUTs/curves/matrices
  must return the original in-domain pixel within epsilon.
- **Fast/slow parity:** LUT, CPU, and GPU paths must agree within perceptual
  tolerance for the same non-spatial recipe.

## Synthetic Corpora

- Neutral gray ramps from black through over-range white.
- Saturated and near-saturated RGB, CMY, orange, and skin-like patches.
- High-chroma over-range highlights.
- Low-luminance chromatic shadows.
- Checkerboards and flat fields for spatial/detail operations.
- Noise fields with controlled luminance and chroma variance.
- Hue rings with small angular deltas around every sector boundary.

## Tool Proof Obligations

### Input Transform

Fields: raw exposure, raw black/white level, raw white balance, camera profile,
camera matching look, calibration hue/saturation, primary hue/saturation,
chromatic adaptation, highlight recovery, gamut mapping, lens correction.

Proofs:

- Black/white normalization maps raw black to 0 and raw white to 1:
  `n = clamp((raw - black) / (white - black), 0, +inf)`.
- Exposure is multiplicative in linear light:
  `p' = p * 2^stops`.
- White balance/adaptation is a 3x3 matrix `M`; neutral camera whites must remain
  neutral after adaptation.
- Highlight recovery must not create negative channels or hue inversion:
  `min(R', G', B') >= 0`.
- Camera/profile/calibration matrices must preserve finite values and must be
  followed by explicit gamut mapping for SDR output.

### Global Tone

Fields: exposure, contrast, highlights, shadows, whites, blacks, brightness,
HDR local tone mapping, filmic shoulder, filmic toe, adaptive contrast, levels
black/gamma/white, zone controls.

Proofs:

- Exposure obeys `Y' = Y * 2^stops`.
- Levels obey:
  `x' = ((clamp(x, b, w) - b) / (w - b))^(1 / gamma)`.
- Tone curves and zones must be monotonic on a neutral ramp.
- Chroma-preserving tone must enforce
  `Y' <= output_headroom * Y / max(R, G, B)`.
- Shoulder/toe must be continuous at their join points:
  `lim_left f(x) = lim_right f(x)`.

### Curves

Fields: parametric curve shadows/darks/lights/highlights, master point curve,
luma point curve, red/green/blue point curves.

Proofs:

- Identity points `{(0,0), (1,1)}` preserve all in-domain pixels.
- Master/luma curves preserve neutral axis: `R = G = B => R' = G' = B'`.
- Master/luma curves are monotonic after sorting/control interpolation.
- RGB channel curves may change hue but must remain finite and destination-safe.

### White Balance

Fields: temperature and tint, including input-transform overrides.

Proofs:

- Neutral order must be preserved: if `Y_i <= Y_j`, then `Y'_i <= Y'_j`.
- WB matrix application must not produce negative display RGB after final gamut
  mapping.
- The white-balanced neutral axis may become warm/cool, but channel excursion must
  still obey `max(R', G', B') <= output_headroom`.

### Color Mixers And Hue Tools

Fields: vibrance, saturation, legacy HSL controls, per-channel mixer in HSL/HSV/
OKLab/LCH, selective CMYK color, RGB channel mixer, black-and-white mixer.

Proofs:

- Saturation at zero preserves the pixel.
- Saturation/vibrance must not change neutral pixels.
- Hue rotation must be circular and continuous:
  `d_hue(a,b) = min(|a-b|, 1-|a-b|)`.
- RGB mixer is matrix multiplication:
  `p' = M p`; identity matrix preserves pixels.
- Black-and-white output is achromatic:
  `abs(R' - G') <= eps` and `abs(G' - B') <= eps`.
- All mixer outputs must be finite and destination-safe.

### Selective Color, Replacement, Skin, Harmony

Fields: color replacement source/target/range/strength, skin tone line/range/
strength, harmony mode/base/strength, selective cyan/magenta/yellow/black.

Proofs:

- Replacement moves selected hue closer to target:
  `d_hue(h_after, h_target) < d_hue(h_before, h_target)` for in-range samples.
- Out-of-range hue samples must change by no more than epsilon at zero falloff.
- Skin protection must reduce hue displacement for protected skin samples versus
  the same recipe with protection disabled.
- Harmony must move selected colors toward the nearest configured anchor without
  discontinuity at hue wraparound.

### Color Grading

Fields: shadows/midtones/highlights hue, saturation, luminance.

Proofs:

- Zone weights must be nonnegative and bounded.
- The sum of zone contributions must not create channel excursions.
- Zero saturation/luminance grading preserves the pixel.
- Gray ramps must remain monotonic in luminance.

### LUTs

Fields: single LUT, LUT stack, strength, domain min/max, input/output color space.

Proofs:

- Identity 3D LUT preserves all pixels inside its domain.
- Trilinear interpolation weights sum to one and are nonnegative:
  `sum(w_i) = 1`, `w_i >= 0`.
- Strength blends as `p' = lerp(p, lut(p), strength)`.
- Domain handling must be explicit; clamping must not hide over-range tone
  defects before output mapping.

### Presence And Detail

Fields: texture, clarity, dehaze, sharpening, sharpen masking, luminance noise
reduction, color noise reduction.

Proofs:

- A perfectly flat field must remain unchanged for texture, clarity, dehaze, and
  sharpening.
- Sharpening kernels must have unit DC gain:
  `sum(kernel) = 1`.
- Masked sharpening must reduce sharpening gain in low-edge areas.
- Luminance noise reduction must reduce luminance variance:
  `var(Y_after) < var(Y_before)`.
- Color noise reduction must reduce chroma variance while preserving mean
  luminance within epsilon.
- Detail tools must not create negative RGB or SDR headroom excursions.

### Optical, Geometry, And Spatial Tools

Fields: lens distortion, lens vignette, chromatic aberration, perspective,
rotation, scale, skew, liquify, crop fill, linear/radial/local masks, retouch
masks, photo layers.

Proofs:

- Identity transforms preserve pixel values and dimensions.
- Vignette gain must be finite and nonnegative.
- Chromatic aberration correction must preserve neutral flat fields.
- Mask opacity/density blending must obey:
  `p' = lerp(base, adjusted, clamp(mask * opacity * density, 0, 1))`.
- Linear/radial masks must be continuous across feather regions.
- Layer opacity/blend modes must keep finite output and respect alpha semantics.

## Implemented Regression Test

`color_tool_invariant_test` currently covers:

- neutral identity behavior
- tone/exposure/contrast/highlight headroom
- middle-gray contrast pivot and positive/negative stop-distance response
- shadow/black/zone monotonicity
- highlight, shadow, white, and black tonal locality
- white-balance neutral-ramp order and headroom
- parametric, master, and luma curve monotonicity
- vibrance, saturation, OKLab mixer, color replacement, skin protection, harmony
- vibrance low-chroma selectivity and hue stability
- three-way grading tonal locality and RGB channel mixer
- black-and-white mixer achromatic output
- identity user LUT preservation
- flat-field presence/detail preservation
- luminance and color noise-reduction variance reduction

The test is intentionally allowed to fail while the engine violates these proof
obligations. A failure is a mathematical defect report, not a flaky visual
preference.
