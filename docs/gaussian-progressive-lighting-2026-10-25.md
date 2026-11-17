# Progressive Scene lighting — 2026-10-25

Scene now displays completed lighting prefixes while the full image refines.
This covers pinhole lighting as well as the first aperture sample, followed by
the completed first aperture frame and the existing later aperture previews.
The final lighting/aperture sample counts, source resolution, opacity model,
surface depth and export quality are unchanged. Moving previews retain the
existing 720-edge limit and eight lighting samples.

Previously a pinhole Scene view did not publish until every lighting sample
completed. The progressive queue was allocated only for multi-sample lenses.
A dense 2700×1799 four-bounce scene therefore showed its first frame after
32.46 seconds, and two 720×480 moving poses after 844/815 ms.

## Behavior and resource ownership

After a complete full-frame lighting prefix, the renderer copies its running
linear surface to a GPU scratch texture. That copy passes through the same
haze, Gaussian raster/composite, lens, post and tone pipeline as the final
image. Finishing a prefix never changes the running accumulator. A published
native output texture is immutable; a fresh output lease is used afterward.
There is no product-preview CPU readback.

Publication uses powers-of-two sample boundaries when the existing batch size
permits them. Spatial paths can show 1, 2, 4, 8… samples; other batched lighting
may first publish at 4/8. These are provisional prefixes of the original target
sequence, not completed lighting solutions. They can change visibly as the rest
of the samples arrive. The preview remains marked as refining. Its caption
shows the displayed lighting sample count rather than internal GPU batch counts.

Only the first aperture sample publishes lighting prefixes. Repeated partial
integration into later aperture sums would double-count a sample. The first
complete aperture frame is also published for batched object lighting; later
aperture prefixes and the final image retain their existing accumulation rules.
Tiled coordinators still suppress incomplete full-frame publication. Source-only
probe rendering and explicit photo/film readback do not publish preview frames.

The viewport consumes the latest immutable frame on its GUI timer. During a
drag it permits a completed live prefix for the current source epoch, then
cancels remaining work if newer camera input is pending. Canceling every
pointer event before the first prefix would starve visible updates. Source
epochs, monotonically published requests, hidden-view cancellation and frozen
exports retain their separate guards. A photo requested from a prefix still
renders the frozen camera/composition at its full requested quality.

No composition schema or project format changes are introduced. New diagnostic
metrics include `progressiveLighting`, `targetLightingSamples`,
`lightingProgressiveFramesProduced` and `firstLightingPreviewMs`.

## Controlled dense comparison

The recipe is frozen from the exposed Main Scene proof: the full 1,244,819-point
Bonsai scene, one inserted receiver, spatial diffuse lighting, four surface
bounces and 16 final samples. Two further poses use the unchanged eight-sample
720×480 live settings. Source, camera, material, resolution, samples and opacity
thresholds are identical before/after. The baseline executable/source are
retained separately.

| Pose | Baseline first frame | Final first frame | Baseline completion | Final completion | Final RGB |
| --- | ---: | ---: | ---: | ---: | --- |
| Photo, 2700×1799 | 32,459.97 ms | 1,827.02 ms | 32,459.97 ms | 32,978.13 ms | Exact |
| Live pose 1, 720×480 | 843.88 ms | 153.93 ms | 843.88 ms | 873.34 ms | Exact |
| Live pose 2, 720×480 | 815.17 ms | 126.98 ms | 815.17 ms | 852.83 ms | Exact |

The predeclared gates pass: at least 75% first-frame improvement, exact final
pixels, completion-time regression below 10%, first photo queue frame below
five seconds, live queue frames below 500 ms, and process RSS inside the 15%
and 128 MiB increase caps. Final first-frame gains are 94.37%, 81.76% and 84.42%.
These are individual **immutable GPU queue availability** observations, not
native input-to-photon percentiles or a clean-machine comparison. Completion is
still about 33 seconds; this does not close the full-quality 20/30-second gates.

Peak process RSS is 1,086,963,712 bytes versus 1,105,575,936 bytes, within the
guard. The broader macOS process footprint rises from 2,175,387,112 to
2,563,655,240 bytes (**17.85%**). This whole-process measurement includes Metal
resources/caches and explicit benchmark inspection readbacks; it is not an
isolated GPU-allocation or steady-state GUI metric. The extra surface copy and
immutable outputs have a real resource cost despite the passing RSS guard.

An initial candidate already passes the speed/runtime/RSS/pixel gates. A second
experiment bounds autoreleased prefix-composition objects but measures
2,565,932,640 bytes versus 2,563,065,440 bytes, missing its predeclared 10%
platform-footprint improvement gate. It is reverted. Two hypotheses are used;
the retained implementation receives a final integration rerun rather than
additional speculative optimization.

Artifacts:
`/private/tmp/looksy-lighting-progressive-plan-v1.json`,
`/private/tmp/looksy-lighting-progressive-baseline-v1/receipt.json`,
`/private/tmp/looksy-lighting-progressive-candidate-v1-comparison.json`,
`/private/tmp/looksy-lighting-progressive-final-perf-v1-comparison.json`.

## Correctness and QPA workflow evidence

Focused native and software traversal checks cover spatial/mixed methods,
probe diffuse with scan visibility, finite soft lights and combined lens/lighting
refinement. Final native pixels and readback exports match independent full
render calls exactly. Every retained prefix is read at publication and again
after completion/later renders to verify immutable GPU texture ownership.
Observed post-publication cancellation returns no final/native frame. Explicit
exports publish no preview frames:
`/private/tmp/looksy-lighting-progressive-native-v2/receipt.json`,
`/private/tmp/looksy-lighting-progressive-software-v2/receipt.json`.

The initial exposed Scene fixture shows its first visible lighting prefix at
129 ms, then accepts a photo export through the viewport API before refinement finishes. A concurrent
composition edit leaves that photo frozen at the original full sample count.
QPA-injected held pointer movement publishes twenty changing live poses, including
six partial frames, with the first live frame at 241 ms. Releasing settles to
the exact latest authored camera at full quality. Clearing the source after a
published prefix rejects all subsequent old-epoch snapshots:
`/private/tmp/looksy-lighting-progressive-ui-v2/receipt.json`.
These are small-fixture observations without a verified foreground precondition;
they do not qualify native input latency. Dense/background and packaged-app
checks are recorded separately.

The first regression run passes 36 tests and fails the new held-input timing
check. A diagnostic rerun records **2033 ms** before the first live frame, then
21 changing poses. That failure is retained:
`/private/tmp/looksy-lighting-progressive-ctest-v1.log`,
`/private/tmp/looksy-lighting-progressive-ui-v3.log`.

The native timing proof explicitly requires an active, exposed window. During
the initial attempts, AppKit reports `active=0` and rejects activation. A real application-loop host was tried
but did not supply activation and disrupted an existing mesh pointer test; that
host change is reverted. A temporary application bundle launched through
LaunchServices and the desktop-control fallback also did not establish a valid
foreground timing run. These attempts do not establish a passing timing result.

The foreground-dependent CTest reports **unavailable (77)** when that prerequisite
cannot be met. It retains the **500 ms** first-held-frame limit when a valid
foreground measurement is possible. An explicit `--allow-background` mode runs
render/export/latest-pose/source-epoch functionality and records
`foregroundTimingQualified=false` when inactive; it does not certify the native
latency gate. A foreground condition lost during the held trace is also rejected.
The original timing failure is not reclassified as a pass.

The final focused regression run passes **37/37 tests**, including the
foreground-dependent test, with no skips or failures:
`/private/tmp/looksy-lighting-progressive-ctest-v3.log`. An additional retained
foreground run reports an active, exposed window through the held trace:
**123 ms** first visible lighting prefix, **245 ms** first held live frame and
**20** changing poses. It passes the unchanged 500 ms limit, full frozen early
photo export, latest-camera settlement and source-epoch rejection:
`/private/tmp/looksy-lighting-progressive-ui-foreground-final-v1/receipt.json`.
This small fixture publishes complete live frames within the held trace, so it
does not demonstrate dense partial-frame behavior. It qualifies one active
foreground trace, not sustained input-to-photon percentiles. Earlier failed and
unavailable observations remain retained.

The full 1,244,819-point dense workflow passes with the explicit **120-second
offline action wait**. At the compact view it shows its first visible prefix at
**795 ms**. Held QPA input produces **11 changing partial frames** and first
live feedback at **301 ms**; the window is inactive, so
`foregroundTimingQualified=false`. It settles to the latest authored camera at
all 16 samples, preserves the frozen early photo recipe despite a concurrent
edit, and rejects stale prefixes after source clear:
`/private/tmp/looksy-lighting-progressive-dense-ui-background-v2/receipt.json`.
This verifies dense functional behavior, not foreground input-to-photon latency.
The unfinished v1 orchestration was interrupted to correct a pending Main input
and supplies no passing dense receipt. The v2 receipt is authoritative.

The frozen desktop bundle displays a lighting prefix after **3601 ms** from
first observed Scene rendering in the ordinary run, while correctly failing
the unchanged 30-second **full completion** gate. The explicit offline check
passes in **40,947 ms**, rendering four bounces at **2700×1799 / 16 samples**
in **32,283.21 ms**, with no preview readback. Its first captured prefix is
**2001 ms** after observed rendering. The app window is exposed and active;
this remains an observed-render-to-capture check, not input-to-photon latency.
`timingGateQualified=false` remains in the offline receipt:
`/private/tmp/looksy-lighting-progressive-main-gate-v1/receipt.json`,
`/private/tmp/looksy-lighting-progressive-main-offline-v1/receipt.json`.

The first legacy reflection Main check reaches the 30-second deadline while
`navigating=true` in live quality, with a different camera from the retained
prior proof. It does not qualify full-quality completion; no attribution to
external input or to a specific product defect is established:
`/private/tmp/looksy-lighting-progressive-main-legacy-v1/receipt.json`.

One diagnostic legacy repeat passes the original 30-second limit in
**26,665 ms**, with the full 2700×1799 render taking **18,181.00 ms** and
`navigating=false`. This confirms full rendering still completes for that recipe,
but does not turn the first failure into a pass or establish repeatability:
`/private/tmp/looksy-lighting-progressive-main-legacy-rerun-v1/receipt.json`.

A new v13 lossless film master and four retained masters each replay all
**12 frames exactly**: **60/60 frames**, maximum channel error **0**. The
retained recipes cover surface paths, spatial diffuse, spatial reflection and
bright-source legacy rendering. This protects frozen export behavior at the
recorded 256-edge recipe, not multi-surface 8K export:
`/private/tmp/looksy-lighting-progressive-sequence-v1/camera-path.json`,
`/private/tmp/looksy-lighting-progressive-sequence-replay-v1/receipt.json`,
`/private/tmp/looksy-lighting-progressive-retained-surface-replay-v1/receipt.json`,
`/private/tmp/looksy-lighting-progressive-retained-diffuse-replay-v1/receipt.json`,
`/private/tmp/looksy-lighting-progressive-retained-reflection-replay-v1/receipt.json`,
`/private/tmp/looksy-lighting-progressive-retained-legacy-replay-v1/receipt.json`.

The qualified packaged executable is the immutable bundle at
`/private/tmp/looksy-lighting-progressive-app-final-v1/Looksy.app`, SHA-256
`3694b34ce59a2c351a15aa47f1d68937999e69387005c12eb261981aef69f634`,
verified unchanged before/after the Main/film runs. A later workspace App rebuild
from concurrent unrelated work is outside this packaged proof. Current scoped
source, test binary, report and evidence hashes are retained in
`/private/tmp/looksy-lighting-progressive-source-binary-final-v1.json`.

Full reference-video parity remains open. Dense completion latency, sustained
input-to-photon behavior, richer post/optical stack performance, representative
capture sharpness/coverage and matched photographic integration remain separate
qualification gates. Faster provisional feedback does not repair missing or
blurred capture, nor establish complete physical or photographic equivalence.
