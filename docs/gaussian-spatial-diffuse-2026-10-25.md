# Spatial diffuse illumination — 2026-10-25

Added surfaces can now sample captured light from their own positions. In
**Scene composition → Lights**, enable **Scan illumination** and choose
**Spatial rays**. The existing environment probe remains the default. The
illumination and reflection methods are independently selectable. Spatial mode
uses the scene's light-sample setting and an indirect-light strength control.

Cosine-weighted rays gather the captured Gaussian radiance, including opacity,
anisotropy, source ordering, directional SH, temporal motion/fading/appearance
and the existing distance haze. They can also reach one added analytic/OBJ
surface, receiving its ambient fill, direct lighting and material emission.
Emission from an added object can therefore illuminate other added surfaces.
It does not relight the captured Gaussians. Further indirect surface bounces
and recursive mirror/refraction paths are not implemented.

## Estimator and compatibility

The incoming radiance uses the existing ordered Gaussian peak-response model,
with its 1/255 particle cutoff, .999/.99 opacity convention and 1e-4 termination.
The bounded heap retains the same distance/source order. Captured sRGB opacity
composition is decoded to linear light before applying the receiver material.
Cosine weighting cancels the Lambertian cosine/PDF, as described in
[PBRT's diffuse sampling discussion](https://www.pbr-book.org/4ed/Reflection_Models/Diffuse_Reflection).
The existing view-dependent Fresnel reserve and approximate direct-light Smith
term remain; this is not calibrated albedo/extinction or a complete physical
path tracer.

Finite rectangle/sphere light emission is handled by the existing light-sampled
direct term. A diffuse ray still sees the source's occluding surface but does
not add that same emission again. Authored material emission is sampled by
the cosine rays because it has no separate direct-light estimator. This keeps
the two source families separate, addressing the double-counting issue
discussed in [PBRT's path tracing chapter](https://www.pbr-book.org/3ed-2018/Light_Transport_I_Surface_Reflection/Path_Tracing).
There is no multiple-importance sampling for small emissive objects; they can
need many samples. Increasing strength does not substitute for more samples.

The ray origin is the actual receiver surface. Captured visibility is intrinsic
to these rays, independently of the separate **Scan shadows** toggle used by
direct/probe lighting. Secondary surfaces use direct/ambient/emission shading;
spatial mode does not silently fall back to the shared probe for their further
diffuse transport. A separately selected probe reflection still uses its probe.

Composition v12 adds `illuminationMode`, retaining explicit reflection mode,
scan-shadow, opacity and complete lighting settings. Both methods persist when
disabled. Probe selection canonicalizes to the applicable older schema. Global
undo/redo, artifact binding, atomic/stale-writer protections, effect edits,
photo/film freezing and global composition on shot recall are preserved. The
renderer uses the spatial ray pipeline even when reflections are disabled;
separate runtime mode flags prevent a diffuse selection from changing the
specular estimator. Legacy/default descriptions remain unchanged.

## Independent checks and rendered demonstration

Twenty rendered cases check white-furnace integration, both alpha caps, linear
strength and zero bypass, local and translated captured fields, a distant-probe
miss, directional SH integration, temporal motion/fading/appearance/revisit,
emissive indirect lighting, first-object independence, a directly lit secondary
surface, two-segment haze, finite-source double-counting and combined/mixed
reflection/illumination methods. Exact tiling, aperture/haze tiling and observed
GPU-batch cancellation are checked as well. The final accepted native and
software receipts each pass all twenty cases with maximum channel error zero.
Exact tiling and cancellation pass on both traversal paths. **31/31 focused
regression tests pass** in the final build:
`/private/tmp/looksy-spatial-diffuse-ctest-v1.log`.

A synthetic native A/B uses the same blue emissive box, gray sphere, floor,
white directional key and camera in both frames. Only the illumination method
changes. Spatial mode transfers blue light to the sphere and floor, affecting
40,595 receiver pixels by the defined color-gain check. The source itself is
excluded from that pixel count. This is a rendered demonstration, not a matched
photographic-quality claim. Its recipe is included in the receipt.
Final accepted native images and receipt:
`/private/tmp/looksy-spatial-diffuse-accepted-native-v1/emission-demo-probe.png`,
`emission-demo-spatial.png`, `receipt.json`.
Software traversal:
`/private/tmp/looksy-spatial-diffuse-accepted-software-v1/receipt.json`.
All 21 retained native/software case and demonstration PNGs are exactly equal:
`/private/tmp/looksy-spatial-diffuse-native-software-comparison-v1.json`.

The ordinary native UI fixture passes mode selection, undo/redo, effect
retention, frozen photo/film settings, concurrent global edits, reopen/recall
and compact controls. A separate ordinary-fixture run also checks independent
combined method selection. Combined dense transport performance is a separate
gate: `/private/tmp/looksy-spatial-diffuse-combined-ui-v1/receipt.json`.

## Dense rendering and explicit limits

At unchanged **2700×1799 and 16 samples**, the full 1,244,819-point Bonsai scene
initially takes 42,590.28 ms cold and 45,162.56 ms at the measured warm pose.
The ordinary **20-second native action wait fails** while illumination is
refining. Failure: `/private/tmp/looksy-spatial-diffuse-public-v1`.

Using a complete execution-width group along each diffuse row reduces the
same recipe to **31,460.20 ms cold / 32,006.59 ms warm**, a 29.13% warm
improvement. All three probe/spatial comparison images are exactly equal.
Peak RSS rises from 1,083,686,912 to 1,124,581,376 bytes (3.77%); the broader
platform footprint rises from 2,439,955,824 to 2,446,280,000 bytes (0.26%).
The first dispatch change passes the predeclared 20% improvement, 10% cold/RSS
and maximum 1/255 pixel guards and is retained. A second specialization/cache
experiment takes 32,584.11 ms warm and is reverted for missing its incremental
gate. No source, resolution, sample count or opacity threshold was reduced.
Plan/comparisons:
`/private/tmp/looksy-spatial-diffuse-dispatch-plan-v1.json`,
`/private/tmp/looksy-spatial-diffuse-dispatch-comparison-v1.json`,
`/private/tmp/looksy-spatial-diffuse-variant-comparison-v1.json`.
These are individual poses, not latency percentiles or a clean-machine run.
**The dense 20-second diffuse gate remains open.**

With an explicitly offline **120-second action wait**, the dense native
workflow completes photo/film export, concurrent edits, reopen/recall and
compact pointer controls:
`/private/tmp/looksy-spatial-diffuse-accepted-offline-ui-v1/receipt.json`.
The final accepted-build run retains a 2048×1364, 16-sample frame at
13,452.87 ms with no preview readback. Its exported photo and compact native
controls were visually inspected. Frozen photo/film composition retains spatial
illumination at strength .7 while concurrent/reopened global strength is .2.
This is completion evidence, not a responsiveness pass.

The final packaged Main app also fails its unchanged **30-second gate**:
`/private/tmp/looksy-spatial-diffuse-main-gate-v1/receipt.json`.
An explicitly requested `--gaussian-scene-offline-proof` uses a separately
labeled 120-second diagnostic wait. It records `timingGateQualified=false` and
completes the same 2700×1799, 16-sample scene with no preview readback: render
32,636.67 ms, proof elapsed 42,345 ms, inactive/exposed window. Its Lights
controls and captured image were visually inspected:
`/private/tmp/looksy-spatial-diffuse-main-offline-v1/receipt.json`,
`environment-lighting.png`. Ordinary Scene proof retains its 30-second limit;
this option does not relax a product timing gate.

The prior spatial-reflection recipe retains exact images and its measured
performance: 17,097.96 ms warm versus 16,815.92 ms, within the 10% guard.
`/private/tmp/looksy-spatial-diffuse-reflection-guard-v1.json`.
The final packaged Main build also passes the unchanged 30-second check for
that existing reflection recipe: active/exposed window, 26,119 ms proof elapsed,
17,599.55 ms render at 2700×1799, 16 samples and no preview readback:
`/private/tmp/looksy-spatial-diffuse-accepted-reflection-main-v1/receipt.json`.
The legacy visibility recipe retains exact pixels at 375.69 ms warm, within its
10% timing/RSS guards with unchanged index size:
`/private/tmp/looksy-spatial-diffuse-legacy-guard-v1.json`.

A new lossless master from the final native workflow freezes composition v12
and spatial strength .7, and all twelve 256×170 frames replay exactly:
`/private/tmp/looksy-spatial-diffuse-accepted-sequence-v1/camera-path.json`,
`/private/tmp/looksy-spatial-diffuse-accepted-new-sequence-replay-v1/receipt.json`.
The earlier diffuse, spatial reflection and bright-source legacy masters also
replay all twelve frames exactly with the accepted renderer:
`/private/tmp/looksy-spatial-diffuse-accepted-sequence-replay-v1/receipt.json`,
`/private/tmp/looksy-spatial-diffuse-accepted-retained-spatial-replay-v1/receipt.json`,
`/private/tmp/looksy-spatial-diffuse-accepted-legacy-replay-v1/receipt.json`.

Higher sample counts, combined dense methods, diffuse 8K export, dense temporal
transport, sustained interaction and matched photographic integration remain
unqualified. Capture completeness/sharpness and full reference-video parity
remain open; these lighting changes cannot repair missing or blurred capture.
Final source/binary/evidence identities:
`/private/tmp/looksy-spatial-diffuse-source-binary-final-v1.json`.
