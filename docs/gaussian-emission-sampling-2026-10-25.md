# Targeted captured-emission sampling — 2026-10-25

**Captured scene → Object emission sampling → Target emitters** combines rays
aimed toward conservative emitter bounds with cosine hemisphere rays. Newly
enabling object emission chooses this method. Existing v14 recipes retain their
cosine estimator, and the native selector offers both methods. Explicit choices
persist when emission reception is disabled and survive reopen/export.

## Estimator and compatibility

Visible emitting spheres/ellipsoids and boxes use conservative spherical bounds.
Imported objects use bounds of their emitting triangles, including nonuniform
scale, rotation and imported material selection. Nonemitting triangles do not
enlarge those bounds. Bounds are proposals only: the actual geometry/material,
visibility and haze still determine each ray's contribution.

The sampler chooses emitters uniformly, samples the bound's solid-angle cone,
and evaluates the complete mixture density across overlapping bounds. A receiver
inside a bound uses full-sphere support. Rays also sample the cosine hemisphere.
The contribution uses `cos(theta) / (pi * mixturePDF)`, with the exact allocation
fractions. For small aggregate bounds, seven of eight strata target emitters;
broad/containing bounds retain a half cosine, half emitter allocation. Keeping
cosine support avoids truncating light paths to a guessed visible surface.
This is a bounded-emitter/cosine mixture estimator, rather than sampling mesh
triangle areas or a complete path-tracer light hierarchy. The general rationale
for combining light and BSDF proposals is described in
[PBRT's path integrator](https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer).

Composition **v15** stores strict `captureEmissionSampling` values `bounds-mixture`
or `cosine`, including an explicit disabled preference. Older schemas and their
rendering recipe remain available. Newly versioned probe rendering also honors
the selected final capture-lighting sample count and uses eight for navigation;
the old v14 rule selected only one probe sample without an explicit soft light.
The probe's quality participates in its cache key, so switching draft/final
cannot reuse an under-sampled probe. Older recipes preserve their former rule
for reproducibility. Captured lighting remains approximate and baked illumination
is not removed.

## Independent evidence

Twenty analytic/quadrature rendered cases pass with maximum channel error
**1/255**. They retain the original emission, occlusion, haze, mesh, temporal,
tiling/progressive and cancellation checks, and add overlapping bounds,
disjoint colored emitters and a receiver inside a bound. Native and software
evidence: `/private/tmp/looksy-emission-importance-native-final-v1/receipt.json`,
`/private/tmp/looksy-emission-importance-software-final-v1/receipt.json`.
All 32 retained PNGs are byte-identical between these traversal backends:
`/private/tmp/looksy-emission-importance-native-software-final-v1.json`.

The small-source experiment uses independent Lambert sphere solid angles and
rectangle view factors, 16 receiver seeds, and unchanged geometry/radiance.
Each budget is compared with the retained cosine estimator:

| Source | Samples | Cosine sum of absolute red-code errors | Targeted sum | Targeted maximum |
| --- | ---: | ---: | ---: | ---: |
| Sphere | 8 | 624 | 0 | 0 |
| Sphere | 16 | 624 | 0 | 0 |
| Sphere | 64 | 624 | 0 | 0 |
| Imported panel | 8 | 704 | 46 | 8 |
| Imported panel | 16 | 704 | 67 | 8 |
| Imported panel | 64 | 704 | 14 | 2 |

The old sampler misses these small sources in the tested domain. The targeted
sphere matches the analytic code at all budgets. The panel's bounding sphere
contains empty directions, so its live draft remains noisier; the default
64-sample result meets the declared 2/255 maximum-error target. This is a bounded
seed/geometry experiment, not a general low-sample convergence guarantee.

An initial equal-allocation panel experiment misses that final target (maximum
3/255 at 64 samples). Concentrating the proposal allocation corrects it. A
two-code target at the eight-sample draft was also too strong for this bound
proposal; draft checks require substantial aggregate improvement, while final
default-quality checks retain the strict two-code target. The failed observations
remain in `/private/tmp/looksy-emission-importance-native-v2.log` and
`/private/tmp/looksy-emission-importance-native-v3.log`.

Strict preference persistence and probe-cache transitions also pass: final 64,
identical final reuse, draft eight without stale reuse, restored final 64, and
explicit v15 cosine probes at final 64.

## Dense timing boundary

The first 1,244,819-point Bonsai UI workflow reaches its frozen film export but
fails the original **20-second** action wait at **4/12 completed frames**:
`/private/tmp/looksy-emission-importance-dense-ui-v1.log`, `failure.png`.
The worker is still exporting at the failed observation; this is a timing-gate
failure, rather than a renderer error. The proof then exits and cancels its work.
An explicitly longer offline run is separate; it cannot turn this observation
into a responsive-film qualification. Emission-directed rays find substantially
more visibility work than rays that miss small emitters. Dense combined-method
performance, sustained foreground interaction and full reference-video parity
remain open.

The separate offline workflow passes with a **120-second per-action wait**,
retaining `timingGateQualified=false`:
`/private/tmp/looksy-emission-importance-dense-offline-ui-v1/receipt.json`.
It exercises real selection of both samplers, undo/redo, mode/effect retention,
concurrent frozen still/sequence exports, save/reopen, global shot recall and
compact pointer controls. Its off-camera source changes 50,240 captured pixels
by more than one code in the 512×341 still, maximum difference 41/255. The
on/off photos were inspected. These differences establish received lighting,
not photographic accuracy.

The final 2048×1364, 64-sample observation takes **4343.91 ms** with scan shadows,
first lighting prefix **4163.65 ms**, maximum lighting batch **17.53 ms**.
This single pose is not sustained interaction qualification. The twelve-frame
256-edge lossless master retains v15, targeted sampling and scan shadows despite
concurrent edits to the working scene.

## Regression scope

The desktop bundle builds and the focused suite passes **44/44 checks**, with
no failures or skips: `/private/tmp/looksy-emission-importance-ctest-v1.log`.
The existing foreground progressive-preview fixture also runs and passes this
time; it is a small older lighting recipe, not dense targeted-lighting input
latency qualification. Native sampler selection, history, retained mode,
export freezing and reopening pass on the small UI fixture:
`/private/tmp/looksy-emission-importance-ui-v1/receipt.json`.

The frozen packaged Main app also passes its unchanged **30-second** Scene
render/control gate in **17,838 ms**, active and exposed, rendering the targeted
scan-shadow recipe at **2700×1799, 64 samples**, **8547.17 ms** render time:
`/private/tmp/looksy-emission-importance-main-v1/receipt.json`, `scene-objects.png`.
The visible result and controls were inspected. This is a packaged Scene proof,
not a dense interaction or full photographic-parity certificate. The immutable
bundle is `/private/tmp/looksy-emission-importance-app-final-v1/Looksy.app`,
executable SHA-256
`a9619c8f10717b98ff56f404d5e192785f125858d4c0220faf06dfb5c0ced275`.

The accepted recipe exports a **2700×1799 twelve-frame** lossless master at 64
samples: `/private/tmp/looksy-emission-importance-sequence-2700-v1/camera-path.json`.
Its first full-resolution frame was inspected. Both that master and the native
UI's 256-edge sequence independently replay all twelve frames exactly:
`/private/tmp/looksy-emission-importance-final-replays-v1/targeted-2700/receipt.json`,
`/private/tmp/looksy-emission-importance-final-replays-v1/targeted-ui/receipt.json`.
This establishes deterministic frozen output at those recipes and resolutions,
not 8K, all material/optical combinations or photographic reference parity.

All seven retained masters also replay their 84 frames exactly, including both
v14 emission masters, progressive lighting, surface paths, spatial diffuse,
spatial reflection and legacy bright sources. The nine final masters total
**108/108 exact RGB frames**, maximum channel error 0:
`/private/tmp/looksy-emission-importance-final-replays-v1/runs.json` and each
named subfolder's `receipt.json`.

Final scoped source/binary/evidence identities:
`/private/tmp/looksy-emission-importance-source-binary-final-v1.json`. The scoped
implementation and test binaries, plus the frozen packaged executable, remain
unchanged throughout final qualification. No source reconstruction gain, matched
photographic lighting integration, dense combined-method latency or full
reference-video parity is claimed.
