# Alpha-aware layer transform sampling

Follow-up: [footprint-aware minification](layer-minification-2026-09-07.md)
widens the filter below unit scale and adds full-resolution-source reduction
evidence. This document records the earlier four-tap checkpoint.

## Defect and implementation

Scaled and rotated pixel, linked-image and text layers used nearest-neighbor
sampling in the legacy CPU stage, compiled CPU reference, and Metal retained-layer
kernel. This produced discontinuous cutout/photo edges and blocky enlargement.
Correct source-over blending alone did not fix this sampling problem.

All three paths now use bilinear reconstruction in the linear working space.
RGB contributions are multiplied by coverage before filtering; filtered RGB is
unassociated afterward for the existing blend stage. Fully transparent texels
therefore contribute no hidden color. For the alpha representation background,
see [Microsoft's premultiplied-alpha documentation](https://learn.microsoft.com/en-us/windows/apps/develop/win2d/premultiplied-alpha).

Outside the finite raster is transparent padding, rather than clamped edge color.
The filter support extends one source texel from each pixel center, giving a
continuous edge instead of the old hard half-texel cut. Aligned integer samples
retain exact source values; the legacy untransformed fast path remains intact.
Negative/HDR RGB is not clamped by the sampler. Existing layer/mask opacity,
clipping and blend-stage behavior is unchanged.

The CPU paths share `color_layer_sampling.h`. Both the checked-in `.metal` file
and the actual runtime-compiled Metal source were changed. The rendered-preview
cache fingerprint advances to `rendered-preview-v8-filtered-layer-transform`.
No source files or recipe schema are changed by rendering.

## Independent numerical regression

`photo_layer_sampling_test` constructs its reference by summing every source
texel's separable tent contribution, independently of the production sampler.
It compares associated RGB and alpha so undefined zero-alpha RGB does not create
false failures. Existing small-alpha compositing early-outs are accounted for.

The 2,160 cases cover four working profiles, pixel/source/text layers, six scales
(0.25 through 7), five rotations (including 0/90/180 degrees), three backdrop
alphas, masked/unmasked opacity, negative/HDR source RGB, hidden extreme colors
in transparent texels, and partially off-canvas placement.

- Before: 1,656 cases fail; maximum associated/alpha error 1.05.
  `/private/tmp/layer-sampling-before-test.log`.
- After CPU: zero failures; maximum 5.05121e-7 (gate 2e-5).
  `/private/tmp/layer-sampling-fixed-test.log`.
- After explicit native Metal: zero failures, same maximum. The test requires
  an available backend and actual GPU readback; it cannot silently pass via a
  fallback. `/private/tmp/layer-sampling-native-test.log`.

## Photographic and output verification

`photo_layer_sampling_image_test` round-trips a linked-image recipe at scale 0.93,
rotation 17 degrees, opacity 0.8 and offset (70,60). Independent geometry uses the
recipe constants rather than compiled uniforms. It compares CPU, retained CPU
and optional native GPU working pixels to a separately expressed associated
sampling equation, then checks actual 8-bit preview versus 16-bit output through
the desktop renderer. It exercises all four working spaces and ACES 2 sRGB output.

Evidence root: `/private/tmp/layer-sampling-photo-YJtiHp/`.

| Input | Maximum CPU error | Retained CPU error | Native Metal error | Preview/output 8-bit difference |
| --- | --- | --- | --- | --- |
| Tagged synthetic cutout with hidden magenta | 5.444e-8 | 2.115e-6 | Not run in this photographic test | 1 |
| Existing Nexus 6 DNG, 640-edge layer | 1.328e-7 | 4.912e-5 | 5.914e-5 | 1 |
| Associated JPEG, 640-edge layer | 5.960e-8 | 4.912e-5 | 5.914e-5 | 1 |

Gates were set before running these images: CPU 2e-6, retained/Metal 2e-4,
preview/output one 8-bit level. GPU coordinates/uniforms use float, while the
independent geometry uses double. The reported errors include alpha and
associated RGB, not a perceptual color metric.

Logs are `cutout.log`, `raw.log`, and `jpeg.log`. RGBA16 and composite PNGs are
saved per working profile in each respective directory. The synthetic cutout and
rotated RAW ProPhoto composites were visually inspected; no magenta fringe was
visible on the cutout. RAW/JPEG original inputs were only read.

The native `metal_aces2_output_lut_parity_test` also passes existing thresholds:
`/private/tmp/layer-sampling-metal-regression.log`. Nine focused tests pass in
7.00 seconds in `/private/tmp/layer-sampling-regressions.log`: both new sampling
tests, both alpha tests, layer color input, layer LUT parity, canonical working
fusion, editor preview math and render parity. The desktop app builds, including
the preview-cache revision; final integration/test builds have no new warnings.
The initial engine rebuild reported the existing `fastMathEnabled` deprecation.

## Remaining work

Bilinear sampling is not high-quality minification: large reductions need
footprint-aware filtering/mip levels to suppress aliasing. This does not prove
full-resolution transform precision, foreground realtime, interactive GPU/UI
latency, stylus behavior, undo/relaunch, or every blend/mask combination. The
photographic test covers one RAW camera and 640-edge layers, not the full RAW
corpus. The color conversion used as input is shared with production and is not
independent colorimetric certification. Scene 4D/film fidelity, embedded inference,
large-catalog loading and full professional editing-tool coverage remain open.
