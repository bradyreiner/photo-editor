# Added-surface light paths — 2026-10-25

**Scene composition → Objects/Lights → Surface bounces** now offers one through
eight secondary surfaces when a spatial reflection or illumination method is
enabled. Added objects can reflect other reflective objects and receive diffuse
light through further added surfaces. Material emission, direct light, ambient
fill, imported OBJ materials/UV color maps and distance haze participate along
the path. The methods remain independently selectable.

The primary receiver is not counted in this limit. One retains the existing
single-secondary-surface behavior. Four permits up to four additional surface
hits after the primary receiver. The path terminates when it escapes, reaches
a finite emitter, has no selected scattering lobe or exhausts the chosen limit.
This is bounded reflective/diffuse transport; transmission/refraction is not
implemented.

## Transport and compatibility

At each secondary vertex, the renderer evaluates local direct light, ambient
fill and emission, then chooses one enabled GGX/cosine lobe. Its weight includes
the reciprocal lobe-selection probability. Energy estimates choose that
probability, bounded away from zero when both lobes are enabled. This keeps
work linear in path depth rather than spawning a binary ray tree. The familiar
BSDF-weight/path construction is described in
[PBRT's path integrator](https://www.pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer).
This implementation has a fixed depth limit, without Russian roulette, MIS or
denoising. Small emissive objects and rough multi-surface paths can need more
samples.

Every bounce has a distinct permutation of the power-of-two sample domain.
Simply changing the azimuth seed while reusing the cosine coordinate at every
vertex correlates the Fresnel factors and over-brightens a deep cavity. The
independent cavity test exposed that error before acceptance. The new reversible
stratum permutation avoids dropping or repeating strata at a vertex.

Finite rectangle/sphere emission remains partitioned between estimators:
diffuse direct lighting samples the lights; diffuse continuation sees their
occluding surfaces without adding the same emission. When spatial specular
continuation is available, its GGX rays handle finite source emission and that
vertex's direct finite-source specular term is omitted. The last shaded vertex
retains its direct estimate. Material emission is evaluated at reached surfaces.

Two or more bounces use **linear radiance and opacity composition on every
captured segment**. Gaussian colors are decoded before alpha composition,
including the existing SH/time appearance and per-segment haze. Probability
weights are never placed inside an sRGB encoding. This is necessary for the
multi-lobe light estimator; retaining a nonlinear terminal-light composition
would distort its probability weights. The existing one-surface mode retains
its trained sRGB alpha convention. Consequently, changing from one to multiple
bounces can also change lighting through partially transparent captured regions.
The primary raster convention is unchanged.

This still uses the existing peak-response Gaussian representation, caps,
cutoffs, stable distance/source ordering and approximate received-material BRDF.
It is not a calibrated extinction/albedo reconstruction or spectral renderer.
In this v13 surface path, captured Gaussians provide radiance and visibility;
authored emission does not relight the captured scan, and its baked lighting
remains. A later, independently selectable [captured-color emission
estimator](gaussian-capture-emission-2026-10-25.md) adds direct authored emission
to captured colors in v14; it does not remove baked lighting.

Composition **v13** adds the strict integer `surfaceBounces` (1–8), with explicit
reflection/illumination mode and the complete existing settings. A value of one
canonicalizes to the applicable older schema. Disabled methods retain the
selected depth. Atomic artifact binding and stale-writer protections are shared
with the existing composition store. Photo/film recipes freeze the depth, while
shot recall preserves current global composition.

The Metal renderer lazily caches a separate multi-surface pipeline and its own
native shadow/radiance intersection tables. Function specialization removes the
new path state from the ordinary one-surface pipeline. The new depth occupies
an existing uniform slot, so the uniform layout remains 48 bytes. Scene changes
release retained particle references in both sets of tables. Progressive batches,
exact global pixel seeds, tiled publication and cancellation remain in use.
Multi-surface metrics explicitly include `surfaceTransportModel`,
`surfaceCaptureModel=linear-opacity-radiance-v1`, `surfaceBounces` and the chosen
reflection/environment depth.

## Independent rendered checks

The native and software traversal paths each pass **19 rendered checks**, with
maximum channel error **1/255** against the independent expectations:

- Diffuse facing-panel cavities at depths 1, 2, 4 and 8, using analytic
  cosine/Fresnel moments and a geometric sequence of emitted contributions.
- Repeated mirror cavities at the same depths, using their independent
  near-perfect-mirror energy series.
- Mixed diffuse/GGX cavities at depths 2, 4 and 8, using direct angular BRDF
  quadrature (128 cosine bins and 256 azimuths), without reproducing the
  renderer's normal sampler, random sequence or lobe selection.
- Per-segment two-bounce haze and captured-opacity composition.
- Rectangle and spherical light emission reflected through two/four surfaces,
  plus exact diffuse finite-source partition checks.

Exact multi-surface tiling, observed GPU-batch cancellation, v13 validation,
single-depth canonicalization, disabled state and save/reopen also pass.
Receipts: `/private/tmp/looksy-surface-bounces-native-v3/receipt.json`,
`/private/tmp/looksy-surface-bounces-software-v3/receipt.json`.
All 19 retained case/demo PNG pairs match exactly between traversal backends:
`/private/tmp/looksy-surface-bounces-native-software-comparison-v1.json`.

The same synthetic blue-emitter/gray-sphere/floor scene renders at depths one
and four. Increasing only depth adds visible blue light transfer to 8,467
receiver pixels under the recorded >3-channel-gain, unclipped-blue criterion.
The receipt includes its editable camera/composition and both render metrics:
`/private/tmp/looksy-surface-bounces-native-v3/emission-depth-1.png`,
`emission-depth-4.png`, `receipt.json`. These native renders were visually
inspected; they are not a matched photographic-quality comparison.

## Native workflow and remaining gates

The desktop bundle builds and **34/34 focused regression tests pass**:
`/private/tmp/looksy-surface-bounces-final-build-v1.log`,
`/private/tmp/looksy-surface-bounces-ctest-v1.log`.

The ordinary native fixture passes the unchanged 20-second action wait with
independent combined methods. Real popup pointer selection, depth undo/redo,
effect retention, frozen photos/films, concurrent global depth edits,
reopen/recall and compact selection of 8 → 1 → 4 all pass:
`/private/tmp/looksy-surface-bounces-ui-v2/receipt.json`.
The frozen export uses four bounces; the concurrent/reopened global value is
two. The final compact selection is four. This is a small-fixture workflow pass,
not dense responsiveness or matched photographic integration.

The full 1,244,819-point Bonsai native workflow also completes with the explicit
**120-second offline action wait**. The final 2048×1364 frame uses four bounces,
16 samples, linear captured transport and no preview readback, taking
15,038.58 ms. Photo/film recipes freeze composition v13 at four bounces and
strength .7; the concurrent/reopened global depth is two and strength .2.
The final compact pointer selection returns depth to four. Exported and compact
captures were visually inspected:
`/private/tmp/looksy-surface-bounces-dense-ui-v1/receipt.json`,
`environment-photo.png`, `surface-bounces-compact.png`.
This dense fixture has one inserted receiver; it verifies the real captured
field and workflow, not dense chains of multiple authored objects.

The packaged Main app also completes the explicitly offline check in an
**exposed, inactive window**, with four bounces at 2700×1799 and 16 samples, no
preview readback: render **34,890.04 ms**, proof elapsed **44,737 ms**.
`timingGateQualified=false` is retained. The ordinary 30-second limit is not
relaxed and this render does not meet it:
`/private/tmp/looksy-surface-bounces-main-offline-final-v1/receipt.json`.
The legacy one-surface reflection recipe still passes its ordinary Main
30-second check: proof elapsed 27,255 ms, render 17,898.28 ms at full resolution:
`/private/tmp/looksy-surface-bounces-legacy-reflection-main-final-v1/receipt.json`.

The shared app was rebuilt by unrelated Timeline work after the first Main
checks. The final two Main checks therefore use an immutable copy of the latest
bundle, with its executable hash verified before/after both runs:
`/private/tmp/looksy-surface-bounces-app-final-v1/snapshot.json`,
`proof-binary.json`. Gaussian sources and focused test executables did not change.
Unrelated Timeline CMake registrations are outside this qualification.

A new v13 lossless master freezes the dense four-bounce film recipe; all twelve
256×170 frames replay exactly. The retained one-surface diffuse, reflection and
bright-source legacy masters also replay all twelve frames exactly:
`/private/tmp/looksy-surface-bounces-sequence-v1/camera-path.json`,
`/private/tmp/looksy-surface-bounces-sequence-replay-v1/receipt.json`,
`/private/tmp/looksy-surface-bounces-retained-diffuse-replay-v1/receipt.json`,
`/private/tmp/looksy-surface-bounces-retained-reflection-replay-v1/receipt.json`,
`/private/tmp/looksy-surface-bounces-retained-legacy-replay-v1/receipt.json`.
This verifies lossless publication and frozen state, not multi-surface 8K export.

## Retained-recipe performance guards

All nine retained one-surface diffuse/reflection/visibility comparison images
remain **exactly equal**. At unchanged source, resolution and sample count,
one-surface reflection takes 16,953.31 ms warm versus 17,097.96 ms, and diffuse
takes 31,620.52 ms versus 32,006.59 ms. Their cold/warm and peak-RSS guards all
remain within 10%:
`/private/tmp/looksy-surface-bounces-legacy-guards-v1.json`.

Legacy visibility warm rendering is 367.69 ms versus 375.69 ms, with unchanged
index size and peak RSS within 10%. Its first cold/setup render is 1270.31 ms
versus 984.02 ms, **missing the 10% cold guard**. Two unmodified, identical-input
reruns measure 1074.11 and 1002.65 ms cold, and 375.01 and 366.99 ms warm; all
three trials retain byte-identical PNGs and warm/RSS guards. The initial miss is
retained, not replaced by a retrospective average or a passing rerun. Cold-start
stability remains unqualified and needs paired baseline/candidate sampling
before attributing it to a source change:
`/private/tmp/looksy-surface-bounces-visibility-repeats-v1.json`.

Full reference-video parity remains open. In particular, dense diffuse
responsiveness, sustained input-to-photon behavior, representative reconstruction
sharpness/coverage, matched photographic integration, shadowed volumes and
emission relighting the captured scan remain unqualified. The bounded surface
path implementation does not repair missing or blurred reconstruction data.
Combined dense multi-object paths, multi-surface lens/temporal transport and
maximum-resolution exports have not been qualified in this stage.
Final source/binary/evidence identities:
`/private/tmp/looksy-surface-bounces-source-binary-final-v1.json`.
