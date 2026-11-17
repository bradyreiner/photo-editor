# Shadowed scene haze

Scene composition → Haze now offers **Shadowed haze**, off by default. Added
analytic and OBJ surfaces block explicit lights along the fog path. Enabling
**Scan shadows** also includes Gaussian visibility. Directional, point,
rectangle and spherical lights participate; the shared light sample count
controls final integration, while interactive drafts use eight samples.

This replaces the unoccluded constant source color with an extinction-weighted
integral along the ray. Base fog is integrated analytically, and light samples
are stratified by extinction mass. Finite-source samples include inverse-square
or solid-angle weighting, one-sidedness and source-segment extinction. The
existing directional haze gain and anisotropic phase convention are retained.

Primary splat fragments now sample a shared camera-ray field at their existing
center depth. Solid objects and visible emitters use actual ray intersection
distances. Backgrounds use the configured background distance.
Spatial reflections and indirect surface paths integrate their own segments;
probe images include the same haze mode. Aperture samples use their own origins.
This is homogeneous single scattering with creative base fill, rather than a
heterogeneous or multiple-scattering volume renderer. It does not recover the
lighting already baked into a capture.

Composition v16 adds a strictly boolean `hazeShadows` field. Earlier schema
versions retain their unshadowed appearance. The setting participates in undo,
artifact-bound persistence, shot recall, resolved-image/probe invalidation and
immutable photo/film export inputs. PLY continues to contain Gaussian data only.

## Verification

`/private/tmp/looksy-haze-work-v1/suite-v4.json` records these completed runs:

| Check | Result |
| --- | --- |
| Native Metal independent volume oracles | Pass, 2.99 s |
| Software BVH independent volume oracles | Pass, 3.64 s |
| Native QML control/export/reopen workflow | Pass, 26.16 s |
| Existing object, lighting, optical and haze regression | Pass, 0.56 s |

The numeric cases cover unoccluded extinction, box/OBJ shadow intervals,
captured and solid depths, a closed-form Gaussian-shadow integral, disabling
scan shadows, point-source quadrature, rectangle surface quadrature and back
faces, spherical-source solid-angle quadrature, finite sun and reflected fog in
one- and multiple-surface paths. Two additional forward/backward phase oracles
are included in the final regression. The largest center-channel error in the
initial fifteen cases is 0.805 of an
8-bit code. Native and software center codes match in all cases. Pinhole tiles
are exact; the tested aperture tiles are also exact. Cancellation after an
observed completed batch publishes no partial frame.

The QML proof uses actual pointer input on the new checkbox at desktop and
compact sizes. It checks undo/redo, quality and effect edits, a controlled photo
comparison, a 12-frame export whose composition remains frozen during a
concurrent edit, save/reopen and shot recall. Captures were inspected:
`ui-v1/haze-shadow-view.png`, `ui-v1/haze-compact.png`.

The compact synthetic frame records 651.24 ms full rendering and a maximum
1.79 ms GPU lighting submission. These are small-fixture results, not dense
scan or sustained foreground performance qualification. Dense photographic
integration, high-quality convergence on complicated blockers and interaction
remain required before claiming reference-video quality.

The rebuilt packaged Looksy app also passes its Main Scene check with the new
mode and checked Haze control visible: `packaged-main-v2/receipt.json`, 4.86 s.
Its window was exposed but inactive, so this does not qualify foreground
interaction. An initial attempt launched a stale `PhotoEditor.app` bundle; it
was closed and excluded. The actual rebuilt bundle is `Looksy.app`.

## Dense scan boundary

`dense-v3/receipt.json` verifies the full 830,842-point trained capture at
640-pixel width with 16 haze samples and native scan occlusion. The images were
inspected. The new render takes **46,824.57 ms**, compared with **100.90 ms** for
unshadowed haze in the same diagnostic. This adds scan visibility and the new
integration together; it is not an isolated optimization comparison. The
largest GPU lighting submission is 29.01 ms across 2,058 batches. The total
latency is unsuitable for smooth dense-scene editing despite bounded commands.

The initial primary path evaluated a light-visibility integral for each visible
Gaussian center, and separately for solid/background pixels. Repeated dense
scan queries dominated that case. Sharing a depth-dependent camera-ray volume
field was the next performance hypothesis, subject to the same numeric, tile,
optical and visible-output checks. That design had not been implemented in the
initial snapshot. The subsequent bounded loop
now reduces the same dense case to **574.40 ms**, with image guards passing and
the full dense native editing/export workflow verified. See
[the controlled performance comparison](gaussian-haze-performance-2026-10-25.md).

## Final source verification

The final shared-renderer suite passes **16/16 tests in 73.38 seconds**, including
the haze UI, both traversal backends, native rendering, objects, scan shadows,
spatial diffuse/reflections, authored emission, multi-surface paths and film
export: `regression-ctest-final.log`. Separate final evidence captures contain
**17** numeric cases, including the forward/backward phase oracles. All 17
native/software PNG pairs are byte-identical. The maximum center-channel error
remains 0.805, and the tested pinhole/aperture tiles remain exact.

One full-resolution neutral held-out camera from the new capture also has
identical RGB pixels between the current renderer and the frozen evaluation
renderer. Their PNG encodings differ; `neutral-identity.log` records zero
maximum and mean channel error. This is one-camera regression evidence, not
closure of the original CUDA rasterizer audit.

`final-verification-identities-before.json` and
`final-verification-identities-after.json` pin ten source files and four current
binaries and verify they did not change during the final checks. The debug
Looksy bundle builds without new compiler warnings.

The later performance snapshot passes **18/18 tests**, now covering eighteen
numeric cases, shared-field resource bounds and native/export cache separation.
Its dense native workflow and twelve exact frozen-sequence replays pass. The
current implementation, timing and qualification boundaries are recorded in
[the performance report](gaussian-haze-performance-2026-10-25.md).

```sh
cmake --build --preset build-dev-macos-debug --target \
  gaussian_haze_test gaussian_scene_cleanup_ui_test photo_editor_app -j 4
ctest --test-dir build/dev-macos-debug \
  -R '^gaussian_haze(_software|_ui)?_test$' --output-on-failure
```
