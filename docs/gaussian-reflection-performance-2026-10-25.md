# Dense Gaussian reflection performance — 2026-10-25

A bounded max-heap reduces the unchanged dense spatial-reflection recipe from
**35,174.30 ms to 16,815.92 ms** at the measured warm pose on this M4 Pro / 48 GB
host. All three baseline/candidate images are exactly equal. The actual dense
SceneWorkspace workflow now passes its original **20-second per-action wait**,
including mode changes, undo/redo, effect retention, frozen photo/film exports,
a concurrent global edit, reopening, shot recall and compact pointer selection.
This closes that action gate on this host; it is not realtime or sustained
input-to-photon qualification, and full reference-video parity remains open.

## Fixed scenario and acceptance

The frozen recipe retains the 1,244,819-point Bonsai artifact, 2700×1799 pixels,
16 reflection/light samples, the same metallic sphere, roughness, camera poses,
lighting and opacity convention. The first image uses the existing probe; the
next two use spatial rays, with a .01-degree yaw change for the warm pose. These
are one cold and one warm spatial pose, not percentiles or a clean-machine run.

Before editing, the bounded loop allowed at most three attempts and required
at least 20% warm improvement, at most 10% cold/RSS regression and maximum RGB
difference 1/255. No resolution, sample count or opacity cutoff reduction was
allowed. The original 20-second product action gate remained a separate actual
UI check. Plan: `/private/tmp/looksy-spatial-heap-plan-v1.json`.

The freshly measured baseline spends **33,087.01 ms** of its 35,174.30 ms warm
call on the GPU. The retained-particle callback kept up to 32 responses in sorted
arrays, shifting up to 31 entries for each successful insertion. The hypothesis
was to reduce repeated callback writes by retaining the same candidates in a
max-heap and sorting the completed batch once. The first heap attempt passes
and is retained. A separate scoped-activity correction below addresses the
remaining packaged-app check; two of the three allowed attempts were used.

| Metric | Fresh baseline | Retained candidate |
| --- | ---: | ---: |
| Cold spatial frame | 35,618.99 ms | 17,220.43 ms |
| Warm spatial frame | 35,174.30 ms | 16,815.92 ms |
| Peak process RSS | 1,116,782,592 B | 1,122,877,440 B |
| Peak platform memory footprint | 2,320,074,096 B | 2,356,626,776 B |
| Maximum RGB channel difference, three frames | — | 0 |

Warm rendering improves **52.19%**. RSS increases **0.55%**; the broader platform
peak increases **1.58%**. Platform peak is reported separately from RSS and is
not a steady-state index or dedicated GPU-memory measurement. Timed runs did
not overlap builds or other GPU tests.

Baseline: `/private/tmp/looksy-spatial-heap-baseline-v1/receipt.json`.
Final candidate: `/private/tmp/looksy-spatial-heap-activity-perf-v1/receipt.json`.
Comparison: `/private/tmp/looksy-spatial-heap-activity-comparison-v1.json`.
The heap-only first candidate takes 17,070.09 ms warm and also passes all guards
with exactly equal images: `/private/tmp/looksy-spatial-heap-comparison-v1.json`.
The small difference between its timing and the final activity-scoped timing
is not attributed to an additional GPU optimization.
Adjacent `*.log` files retain `/usr/bin/time -l` counters; the comparison helper
is `/private/tmp/looksy-spatial-heap-compare.py`. Pre-edit sources and the
benchmark executable are retained in
`/private/tmp/looksy-spatial-heap-baseline-source-v1`.

## Preserved rendering contract

The heap root is the farthest retained `(peak distance, original source ID)`.
Replacing it moves logarithmically many entries. Heap extraction produces the
same ascending order before opacity compositing. Native and software traversal
use the same key, capacity and final order. Native search bounds still tighten
only at a retained primitive's actual peak. Partial batches resume after their
actual last response on the original physical ray segment.

The physical peak calculation, selected alpha cap, 1/255 particle cutoff,
1e-4 transmittance termination, SH/temporal appearance, GGX samples, reflected
objects/materials/emitters and haze are unchanged. No particle/batch limit is
introduced. Legacy shadow-only specialization is separate.

Both backends pass 28 independent rendered cases with maximum channel error
zero, including the new permutation of 96 distinct depths forcing retained
buffer replacements and three independently summed color layers. Existing
equal-depth, origin/endpoint, 400-particle, roughness, texture, aperture/haze,
exact tiling and observed-batch cancellation checks also pass. Receipts:
`/private/tmp/looksy-spatial-heap-activity-native-v1/receipt.json`,
`/private/tmp/looksy-spatial-heap-activity-software-v1/receipt.json`.
All pixels of the 28 native/software pairs are exactly equal:
`/private/tmp/looksy-spatial-heap-native-software-comparison-v1.json`.
The final **28/28 focused regression tests pass** in 80.20 seconds:
`/private/tmp/looksy-spatial-heap-activity-ctest-v1.log`. The debug bundle and
relevant targets build without new compiler warnings.

The legacy visibility recipe also preserves all three images exactly, at
**364.50 ms** warm versus 365.41 ms previously, and 989.92 ms cold versus
1088.71 ms. Peak RSS is 1,084,260,352 bytes versus 1,083,916,288 bytes, passing
the protected 10% guard. Receipt:
`/private/tmp/looksy-spatial-heap-legacy-perf-v1/receipt.json`;
comparison: `/private/tmp/looksy-spatial-heap-legacy-guard-v1.json`.

The new lossless master and both retained spatial/legacy masters replay every
one of their twelve frames with maximum RGB error zero and matching integrity
hashes. Export: `/private/tmp/looksy-spatial-heap-sequence-v1`.
Replay receipts: `/private/tmp/looksy-spatial-heap-sequence-replay-v1`,
`/private/tmp/looksy-spatial-heap-retained-spatial-replay-v1`,
`/private/tmp/looksy-spatial-heap-legacy-replay-v1`.
These 256×170 checks preserve integration and lossless publication; they do not
extend the earlier 8K proof to spatial-reflection 8K export.

## Native workflow and remaining scope

The unchanged full dense UI workflow passes with `waitLimitMs=20000`:
`/private/tmp/looksy-spatial-heap-activity-public-v1/receipt.json`. Its final 2048×1364,
16-sample frame takes **8,879.74 ms**, with no preview readback. Its largest
lighting submission is 9.67 ms; the scheduler's 8 ms target is not a guaranteed
latency bound. Export sizes and quality settings were not reduced to pass.
The heap-only workflow also passed before the activity correction, with its
final frame at 11,216.84 ms: `/private/tmp/looksy-spatial-heap-public-v1/receipt.json`.
Both are individual workflow observations, not repeated-pose timing percentiles.
The final photo and twelve-frame MP4 retain spatial mode and .7 environment
strength while the concurrent global edit switches to probe mode and .2:
`/private/tmp/looksy-spatial-heap-interchange-v1.json`. Native controls and the
foreground/background packaged captures were visually inspected.
The final 800×533 photo also exactly matches the earlier slow implementation's
photo: `/private/tmp/looksy-spatial-heap-photo-comparison-v1.json`.

### Packaged render activity

The heap-only packaged Main run still times out at its unchanged 30-second
limit, with 7,539 completed lighting batches and no published frame:
`/private/tmp/looksy-spatial-heap-main-v1/receipt.json`. Its timer slowdown is
consistent with background power heuristics; App Nap status was not directly
queried, and that failure receipt does not record window activation.

The public Metal render call now owns a scoped `NSProcessInfo` activity using
`NSActivityUserInitiatedAllowingIdleSystemSleep`, following
[Apple's guidance for lengthy asynchronous user-requested work](https://developer.apple.com/library/archive/documentation/Performance/Conceptual/power_efficiency_guidelines_osx/PrioritizeWorkAtTheAppLevel.html).
RAII ends the activity on success, cancellation, error or exception. Idle system
sleep stays allowed. No persistent power preference or global latency-critical
policy is introduced. This activity declares the work's lifecycle; it does not
change samples, render math or output size.

The rebuilt packaged Main app passes the unchanged limit in both observed
window states, at 2700×1799 and 16 samples, with no preview readback:

| Launch | Proof elapsed | Render call | Window state |
| --- | ---: | ---: | --- |
| Direct executable | 25,352 ms | 17,542.33 ms | Active and exposed |
| Explicit `open -g -n -W` | 25,223 ms | 17,346.40 ms | Inactive and exposed |

Receipts/captures: `/private/tmp/looksy-spatial-activity-main-v1`,
`/private/tmp/looksy-spatial-activity-background-main-v1`. These prove the
current packaged render/control gate in foreground and background; they are
not a controlled measurement of App Nap itself, catalog/agent readiness or
sustained interaction. The initial timing failure is retained.

The earlier performance failures remain in the
[spatial-reflection implementation report](gaussian-spatial-reflections-2026-10-25.md).
Spatial diffuse radiance, recursive reflections, emissive indirect lighting,
shadowed volumes, representative reconstruction completeness/sharpness,
matched photographic comparisons and sustained native interaction remain open.

Final source/binary and evidence identities, together with explicit unresolved
whole-goal gates, are retained in
`/private/tmp/looksy-spatial-heap-source-binary-final-v1.json`.
