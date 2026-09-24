# Viewer GPU performance follow-up

Priority: live Gaussian viewer and settled film/lens rendering, not page switching.
Use the bounded performance-loop skill with at most three attempts. GPU target:
at least 20% lower median GPU time across the same thirty SUV motion poses.
Guards: exact image hashes, no point/resolution reduction, no more than 10%
end-to-end p95 regression in comparable runs, focused native tests and actual
viewer proof. A projection cache, if needed, must remain bounded by the existing
two-million-point admission limit and at most 128 MiB of additional storage.

Artifact root: `/private/tmp/framehouse-viewer-gpu-perf-ghHbUi`.
Baseline binary: `baseline-renderer`; receipt: `baseline.json`.
The existing `--motion-perf` benchmark uses the actual million-splat SUV at
537 × 720, with readback outside the measured render call and hashes of every
output. Baseline median GPU 8.13 ms, CPU setup 33.50 ms, render 42.93 ms;
render p95 45.34 ms. No Brush or production Framehouse process was found in the
process check. This Debug build has no CPU optimization flag; results must not
be represented as Release performance. CPU setup now dominates this scene's
motion path, unlike the previously GPU-contended run.

Attempt 1 hypothesis: use a four-vertex triangle strip for the same splat quad
instead of six independent triangle vertices, reducing repeated projection/SH
evaluation while retaining the same two triangles and compositing order.

Attempt 1 reverted: only 12/30 output hashes matched; measured GPU/render latency
also regressed. No quality tolerance was silently loosened to keep it.
Attempt 2 hypothesis: pixels whose front-to-back transmittance already reached
zero cannot receive any later contribution. Discard subsequent fragments before
Gaussian evaluation/framebuffer writes, keeping the original triangle topology.

Attempt 2 reverted: 30/30 hashes match, but median GPU time did not improve
(32.28 ms baseline versus 41.88 ms candidate in a fresh consecutive comparison).
A separate user's Brush job started during this turn and was left untouched.
Stop GPU-speed attempts because changing contention prevents a reliable gate.

Attempt 3 follows the separately measured CPU bottleneck: retain compact xyz
positions for static-scene depth calculation, avoiding a scan of covariance/SH
records every camera frame. Target at least 25% lower median CPU sort/setup,
exact thirty-frame hashes, at most 24,000,000 bytes extra retained storage under
the existing two-million-point admission cap. Temporal positions retain their
existing path. Cold setup and end-to-end latency remain protected diagnostics;
background training prevents clean-hardware GPU certification.

## Final decision and proof

Keep only attempt 3 as a development optimization. Both shader changes are
reverted; the original six-vertex rasterization, fragment shader, float precision,
all points and image resolution remain unchanged. The three-attempt loop is
closed. No GPU or overall realtime promotion claim is made.

| Fixed thirty-pose comparison | Baseline | Compact positions |
| --- | --- | --- |
| Median CPU sort/setup, first pair | 38.34 ms | 25.33 ms |
| Renderer p95, first pair | 120.47 ms | 98.00 ms |
| Cold renderer, first pair | 160.06 ms | 130.35 ms |
| Median CPU sort/setup, reverse-order repeat | 36.39 ms | 27.12 ms |
| Renderer p95, reverse-order repeat | 113.92 ms | 101.97 ms |
| Cold renderer, reverse-order repeat | 116.13 ms | 134.35 ms |

CPU improvement is 33.95% and 25.49% respectively. All thirty hashes match in
both pairs. Receipts are `baseline-cpu.json`, `compact-centers.json`,
`baseline-cpu-repeat.json` and `compact-centers-repeat.json`. The repeat overlapped
a short test build during startup; neither pair is a clean Release benchmark.
Cold time is variable and worsened 15.7% in the repeat, so cold-start certification
remains open. Warm end-to-end p95 clears its regression guard in both pairs.
The verified CPU gain does not establish a GPU speedup or a clean overall latency
gate under background training.

The retained stream is 12,000,000 bytes for this million-point scene, at most
24,000,000 bytes at the two-million-point cap. Replacement is built separately
and only committed alongside the matching uploaded scene, preserving the prior
scene on cancellation. Old and new streams can temporarily coexist during scene
replacement (up to 48,000,000 bytes at the cap). Temporal scenes release the
stream and retain their existing time-dependent depth path. This is bounded
allocation evidence, not an app-wide RSS/memory-pressure test.

Six native tests pass in 3.59 seconds: viewport handoff, scene rendering,
spacetime rendering, saved views, camera paths and film export. Added tests
exercise different static scene positions/counts, canceled scene admission,
temporal cache release and dense bright overdraw behind terminated pixels.
Existing film/lens, output parity and draft/source isolation checks remain.

The actual `Main.qml` SUV viewer proof passes and is foreground-eligible:

- `ui-final/receipt.json`: 26 published motion drafts in two seconds (about
  13 fps), 1,570 ms settling, film-control feedback 24–58 ms to two subsequent
  frame swaps. Resolved CPU setup is 26.29 ms; GPU time is 1,348.33 ms across
  sixteen aperture samples. These are not input-to-photon measurements.
- `ui-baseline/receipt.json`: 19 drafts in two seconds, 1,353 ms settling,
  resolved CPU setup 54.16 ms. The input cadence is wall-clock driven, so the
  final camera poses are not pixel-parity comparisons. Training contention
  prevents treating their difference as a controlled FPS/settling speed ratio.
- `ui-final/resolved-after-motion.png` was visually inspected. The full-screen
  viewer resolves the film/lens photo after orbit; existing reconstruction
  softness/artifacts remain. No film calibration improvement is claimed.

Production app bundle/process, original photos and unrelated training were not
changed. Only native test targets were rebuilt. `git diff --check` passes.

Remaining work: a representative optimized-build CPU/GPU baseline, GPU-side
ordering/projection investigation where measured useful, and progressive lens
refinement. Current viewer motion and settled-photo latency still miss realtime
interaction goals; this checkpoint is not completion of the full app goal.
