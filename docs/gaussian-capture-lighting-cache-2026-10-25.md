# Capture-lighting reuse across probe views — 2026-10-25

Reflection/environment probe faces now share a completed captured-lighting field
when their inputs match. The existing `shade_capture` kernel depends on observer
position, scene/time, lighting/composition, alpha convention and integration
quality/grouping; it does not depend on view rotation, intrinsics or output grid.
The six cube faces share one observer. Previously each face evaluated the same
field again before rasterizing its own view.

The renderer retains the existing single illumination buffer and a small key.
Scene replacement clears the key. The key includes the full composition,
observer position, alpha model, actual sample count and batch grouping, plus
capture time. Including grouping preserves the existing float accumulation
order when a mixed probe/spatial recipe uses different batch sizes. This is
conservative: some unrelated composition edits still invalidate the field.

The key is cleared before writes and published only after a complete first GPU
pass. Cancellation cannot leave an old valid key attached to a partially
overwritten field. No extra scene-sized lighting buffer, reduced sample count,
new recipe version, shader or rendering approximation is introduced.

## Controlled performance loop

The predeclared plan and frozen baseline driver are under
`/private/tmp/looksy-capture-lighting-cache-plan-v1`. The existing dense
`gaussian_progressive_lighting_test` route loads all **1,244,819 Bonsai Gaussians**,
a visible reflective receiver, off-camera emitter, targeted received emission,
scan shadows, 64 final samples and probe reflections. One full 2700×1799 pose is
followed by two 720×480 eight-sample live poses. Inputs and image conventions
are identical before/after.

The bounded loop permits three hypotheses. Its first hypothesis passes all
predeclared gates: at least 40% full-completion improvement, 30% first-live
improvement, at most 10% second-live regression, exact final RGB, peak RSS and
footprint growth at most 10%, and RSS growth at most 128 MiB. It is accepted;
no additional optimization is stacked onto this loop.

| Pose | Before first queue frame | After first queue frame | Before completion | After completion |
| --- | ---: | ---: | ---: | ---: |
| Full photo | 30,992.86 ms | 9115.09 ms | 31,272.58 ms | 9383.56 ms |
| First live pose | 4020.07 ms | 1332.42 ms | 4035.07 ms | 1347.55 ms |
| Second live pose | 558.45 ms | 555.79 ms | 573.59 ms | 570.89 ms |

Full completion improves **69.99%**; first-live completion improves **66.60%**.
Probe preparation falls from **26,781.62 to 4930.19 ms** for the full pose and
**3456.80 to 776.10 ms** for the first live pose. Each rebuilt probe records one
capture-lighting evaluation and five cache hits. The second live pose already
reuses the entire probe and stays within its guard.

All three final PNGs and all three retained progressive PNGs match byte-for-byte.
Peak process RSS is **1,099,366,400 → 1,100,382,208 bytes**; broader Darwin peak
memory footprint is **2,256,258,368 → 2,258,896,192 bytes**. Both pass their gates.
Footprint is reported separately from RSS and is not dedicated GPU-memory proof.

Artifacts:

- `/private/tmp/looksy-capture-lighting-cache-baseline-v1/receipt.json`
- `/private/tmp/looksy-capture-lighting-cache-candidate-v1/receipt.json`
- `/private/tmp/looksy-capture-lighting-cache-comparison-v1.json`
- Matching `.log` files retain native `/usr/bin/time -l` memory observations.

These are individual controlled renderer/queue observations, not clean-machine
statistics or native input-to-photon percentiles. Live latency remains above the
500 ms target here. The change does not repair capture alignment/detail or
establish photographic integration.

## Correctness and workflow

Both native and software visibility backends pass **34 exact fresh-renderer
comparisons** across view/grid/exposure, observer position and hemisphere,
isotropic fallback, sample quality, lighting/visibility/haze, emitter geometry
and imported materials, scene identity and temporal changes. Sampled/unbatched
transitions, cancellation during an update, returning to the previous recipe,
and mixed probe/main accumulation grouping also pass. All 34 retained PNGs
match byte-for-byte between traversal backends:

- `/private/tmp/looksy-capture-lighting-cache-native-final-v2/receipt.json`
- `/private/tmp/looksy-capture-lighting-cache-software-final-v1/receipt.json`
- `/private/tmp/looksy-capture-lighting-cache-native-software-v1.json`

The native combined reflection/emission workflow passes on the small fixture
and the dense Bonsai scene, including actual sampler choices, history, mode and
effect edits, frozen still/sequence exports during concurrent edits, reopen,
global shot recall and compact controls. The dense run uses an explicit
120-second per-action offline wait and retains `timingGateQualified=false`;
it does not close the prior 20-second dense-film gate. The scene, visible
reflective receiver and captured lighting were inspected:

- `/private/tmp/looksy-capture-lighting-cache-ui-v1/receipt.json`
- `/private/tmp/looksy-capture-lighting-cache-dense-ui-v1/receipt.json`

## Packaged Scene deadline

The retained pre-cache packaged app times out on the combined scene at
**30,091 ms**, still refining without a displayed frame. The optimized frozen
bundle passes the unchanged **30-second** Scene render/control check in
**23,360 ms**. Its 2700×1799, 64-sample render takes **11,792.08 ms**, including
**6474.33 ms** probe preparation, one lighting evaluation and five face hits:

- `/private/tmp/looksy-capture-lighting-cache-main-baseline-v1/receipt.json`
- `/private/tmp/looksy-capture-lighting-cache-main-candidate-v1/receipt.json`

Both project copies start from the same accepted dense UI project. The optimized
Scene and visible reflective receiver were inspected. The candidate window is
exposed but inactive; this is Scene render/control completion, not foreground
input latency or whole-app qualification. The immutable candidate app and both
starting project copies are retained under
`/private/tmp/looksy-capture-lighting-cache-app-final-v1`, with binary identities
in `binary-before.json`. The separate controlled renderer comparison above is
the measured acceptance basis for the optimization.

The desktop bundle builds. The final focused suite finishes with **46 passed,
0 failed and 1 skipped** out of 47 checks:
`/private/tmp/looksy-capture-lighting-cache-ctest-v1.log`. The skipped existing
foreground progressive-lighting fixture could not activate its window. The
native object/reflection/diffuse/surface UI checks pass, but run slower than in
the previous session; they are functional results, not a general UI performance
improvement claim. The optimization's acceptance remains scoped to the frozen
controlled comparison and its explicit guards.

The new combined reflective/emissive native export and all nine retained masters
independently replay **120/120 frames exactly**, maximum RGB channel error 0:
`/private/tmp/looksy-capture-lighting-cache-final-replays-v1/runs.json` and each
named subfolder's `receipt.json`. Retained recipes include full-resolution v14
and v15 emission, progressive lighting, repeated surface paths, spatial diffuse,
spatial reflection and legacy sources. This protects those frozen recipes;
it does not qualify every imported material, optical stack or 8K combination.

Scoped source, executable, report and evidence identities are retained in
`/private/tmp/looksy-capture-lighting-cache-source-binary-final-v1.json`.
The optimized renderer and shader identity remain unchanged through final
qualification; the shader also matches the frozen pre-cache source.

Observer position changes still invalidate the field as a whole. This leaves
warm navigation, first live feedback, dense combined methods and responsive film
export open. Capture alignment/sharpness, shadowed volumes and matched
photographic integration remain separate unfinished work. **Full reference-video
capability and quality parity remains false.**
