# Spatial Gaussian reflections — 2026-10-25

The later [heap traversal and scoped render activity qualification](gaussian-reflection-performance-2026-10-25.md)
closes the dense 20-second action gate and packaged Main 30-second gate on the
measured host. The initial failures below remain as historical evidence.

Added objects can now reflect nearby captured detail from their actual surface
positions. In **Scene composition → Objects**, enable **Scan reflections** and
choose **Spatial rays**. The existing environment-probe method remains the
default. Reflection samples share the scene's lighting sample setting; rough
surfaces and small bright emitters need more samples.

Spatial rays support captured SH appearance, transparent overlap, temporal
motion/fading and temporal appearance, the selected opacity convention,
captured-color lighting and the existing distance haze. Reflected analytic/OBJ
surfaces receive direct lighting and optional probe diffuse illumination.
Imported MTL color, emission and UV color maps are applied. Rectangle and sphere
emitters remain visible to reflection rays when hidden from the primary camera;
disabling the light hides it from both.

The mode is global and undoable. Composition v11 strictly encodes
`reflectionMode: "spatial"` with the complete lighting/opacity settings.
Switching to the probe canonicalizes to the applicable older schema. Photo and
film workers freeze the complete composition, independently of subsequent
global edits. Shot recall keeps the current global composition.

## Rendering contract

The scan's retained inverse scale/rotation factors give the maximum Gaussian
response along each finite reflected ray. A bounded hit buffer collects the
up to 32 nearest responses, ordered by peak distance then original source index. Queries
repeat until no responses remain or transmittance reaches 1e-4. There is no
silent particle or batch count limit. The per-particle cutoff is 1/255, with the
selected .999/.99 alpha cap. Conservative bounds use the same cutoff.

Each subsequent acceleration query begins one float before the preceding peak,
retaining equal-distance IDs and clamped endpoint responses. The physical
segment remains unchanged when computing response; shrinking the search window
must not move a Gaussian peak. Both native Metal acceleration structures and
the software median BVH implement this contract. Original source IDs survive
software reordering and address the same captured SH coefficients.
Native search bounds can tighten after eight responses while retaining up to
32 closer responses. Partial batches resume after their actual final peak, so
this changes traversal cost without changing the composited particle order.
Distance/order rejections precede opacity evaluation and insertion work.

This follows the maximum-response and ordered-buffer approach described in
[3D Gaussian Ray Tracing](https://arxiv.org/html/2407.07090v1), with independent
Metal implementation and the existing captured sRGB opacity-compositing
convention. Composed captured color is converted to linear light for the
receiving BRDF. This does not introduce a physically calibrated volume.

Roughness uses the [GGX visible-normal sampler](https://jcgt.org/published/0007/04/01/)
and height-correlated Smith weight. Primary finite-emitter specular transport
uses these BSDF rays; its separate light-sampled term retains diffuse transport
and delta-light specular. Counting finite-source specular in both estimators
would double the contribution. No multiple-importance sampling is implemented,
so a small bright source on a rough surface can have substantial variance.

The spatial shader is specialized separately from the legacy shader. Its larger
radiance payload does not enter the legacy shadow-only pipeline. Native shadow
and radiance function tables belong to their matching compute pipeline.
Scene/time changes atomically replace the shared visibility index; camera,
light and material edits reuse it. Disabled/empty indices bind valid dummy
resources, and indirect particle reads are explicitly declared to Metal.

## Qualification

The final independent native and forced-software tests pass 27 rendered cases each with maximum
8-bit channel error zero. Cases include single opacity, both caps, equal-depth
ordering, 400 transparent particles, origin and finite-endpoint
clamping, distance order, independence from the first-object probe anchor,
SH degrees 1–3, temporal appearance/motion/fading/revisit, approximate captured
lighting, two-segment haze, reflected surfaces/emission/OBJ textures, finite
source visibility, rough white-furnace quadrature and rough emitter-area
quadrature. Exact tiling and aperture/haze tiling have maximum error zero;
cancellation follows an observed completed lighting batch and publishes no
partial frame. Receipts: `/private/tmp/looksy-spatial-final-native-v1/receipt.json`,
`/private/tmp/looksy-spatial-final-software-v1/receipt.json`.
Every pixel of all 27 paired native/software images is identical:
`/private/tmp/looksy-spatial-native-software-comparison-v1.json`.

**28/28 focused tests pass**, in 137.66 seconds:
`/private/tmp/looksy-spatial-ctest-v1.log`. The packaged debug application and
all relevant test targets build. One existing copied-`QString` range-loop
warning remains in `gaussian_viewport_handoff_test.cpp`; no new spatial-source
compiler warning was emitted.

The initial native UI fixture passes pointer mode selection, undo/redo, effect
retention, frozen photo/film settings despite a concurrent global edit,
reopen/shot recall and compact-window pointer interaction:
`/private/tmp/looksy-spatial-ui-v1/receipt.json`. This small fixture is not dense
performance or photographic qualification.

With an explicitly offline **120-second per-action wait**, the final full dense
workflow also passes pointer mode selection, undo/redo, effect retention,
photo/film export, a concurrent global edit, reopen/shot recall and compact
pointer selection. Its final 2048×1364 frame takes **23,803.84 ms** and uses
16 samples without preview readback. Receipt:
`/private/tmp/looksy-spatial-public-final-v1/receipt.json`.
The 800×533 photo and native compact controls were visually inspected. The film
contains twelve 256×170 frames at 24 fps with BT.709/sRGB tags. Both photo and
film retain spatial mode and environment strength .7 despite the concurrent
change to probe mode and .2 strength. Independent metadata receipt:
`/private/tmp/looksy-spatial-interchange-v1.json`.
The extended wait is completion evidence, not a responsiveness pass.

The final packaged Main application **fails its unchanged 30-second Scene
render/control proof** while the first spatial frame is still refining:
6,854 lighting batches completed, `frameAvailable=false`. Receipt and native
capture: `/private/tmp/looksy-spatial-main-final-v1/receipt.json`, `failure.png`.
The bundle builds, but this run is not a passing integrated Main render or
foreground/sustained-interaction qualification. The limit was not extended.

A lossless master exported from the frozen dense film recipe independently
decodes and replays all twelve frames with maximum RGB error **zero**, including
file/RGB integrity checks. It retains the frozen .7 environment and spatial mode.
Master: `/private/tmp/looksy-spatial-sequence-final-v1/camera-path.json`;
replay: `/private/tmp/looksy-spatial-sequence-replay-final-v1/receipt.json`.
The earlier bright-source legacy master also replays all twelve frames exactly:
`/private/tmp/looksy-spatial-legacy-replay-final-v1/receipt.json`.
These 256×170 checks establish integration and lossless publication; they do
not qualify spatial-reflection export at the earlier 8K gate.

The initial full 1,244,819-point Bonsai workflow **fails** the existing
20-second action wait. Retained failure:
`/private/tmp/looksy-spatial-public-v1`. An extended UI diagnostic was manually
stopped to isolate rendering; it is not a pass. The isolated unchanged
2700×1799, 16-sample baseline completes: cold spatial 89,249.29 ms, warm spatial
88,969.2 ms. Baseline images and metrics:
`/private/tmp/looksy-spatial-dense-baseline-v1/receipt.json`.
The retained traversal reduces cold rendering to **35,605.04 ms** and the
single measured warm pose to **34,764.35 ms**, with **zero changed RGB values**
across all three baseline/candidate images. Resolution, source artifact,
camera poses, lighting, material, opacity convention and 16 samples are
unchanged. Receipt: `/private/tmp/looksy-spatial-dense-hybrid-v1/receipt.json`;
pixel comparison: `/private/tmp/looksy-spatial-hybrid-comparison-v1.json`.
This is one warm pose, not a latency percentile, clean-machine benchmark or
realtime qualification. The largest measured candidate command is 13.93 ms;
the adaptive 8 ms target is not a guaranteed GPU-latency limit.

| Traversal experiment | Cold spatial | Warm spatial | Disposition |
| --- | ---: | ---: | --- |
| Initial eight-hit query from ray origin | 89,249.29 ms | 88,969.23 ms | Baseline |
| Advance acceleration-query minimum | 75,933.88 ms | 75,026.15 ms | Retained |
| Single-hit payload | Not completed after 117 s | — | Interrupted and rejected |
| 32-hit buffer | 40,947.50 ms | 40,293.23 ms | Retained |
| Reject range/order before opacity/insertion | 36,702.42 ms | 35,832.57 ms | Retained |
| One execution group across each row | 38,515.29 ms | 38,140.38 ms | Reverted |
| Bound partial native batches after eight hits | 35,605.04 ms | 34,764.35 ms | Retained final |

Each completed retained version matches the frozen baseline images exactly.
The single-hit run is explicitly not a completed image comparison. Timings
were isolated from builds and competing GPU tests. Evidence folders use
`looksy-spatial-dense-{baseline,marching,k1,k32,early,width,hybrid}-v1` under
`/private/tmp`; source snapshots accompany experiments. No sample count,
output size or opacity cutoff was reduced. **The 20-second dense action gate
remains open.**

The legacy native visibility recipe retains its measured performance and
pixels: warm median **364.16 ms**, versus 365.41 ms previously; cold **996.01 ms**,
versus 1088.71 ms. All three paired frames are exactly equal. Peak process RSS
is 1,083,555,840 bytes, versus 1,083,916,288 bytes; the broader platform peak
is 2,018,051,392 bytes. The predefined 10% cold/warm/RSS guards pass. This is
the unchanged visibility recipe, not spatial-reflection memory qualification.
Receipt: `/private/tmp/looksy-spatial-legacy-perf-v1/receipt.json`;
comparison: `/private/tmp/looksy-spatial-legacy-guard-v1.json`.

## Remaining physical and product limits

Only one reflected surface receives shading; recursive mirrors, refraction,
emissive indirect illumination and multiple-bounce transport remain open.
Diffuse scan illumination still uses the existing shared probe. Captured
appearance retains baked lighting, trained opacity and a raster-trained field;
it is not recovered albedo, extinction or unbiased scene transport. Optional
capture relighting retains the main-camera-oriented approximate normals.
Delta-light and secondary direct shading retain the existing approximate
Smith term; the visible-normal reflection estimator uses correlated Smith.
Reflected texture filtering uses an approximate ray-cone footprint, not full
reflection differentials. Haze remains unshadowed distance scattering.

Source reconstruction completeness/sharpness, matched photographic comparisons,
sustained native interaction and full reference-video parity remain separate,
unfulfilled gates. A rendered synthetic oracle or a build does not close them.

Final source, binary, evidence and report hashes are recorded in
`/private/tmp/looksy-spatial-source-binary-final-v1.json`. It explicitly retains
false qualification for the dense 20-second action gate, packaged Main's
30-second gate, sustained interaction and full reference-video parity.
