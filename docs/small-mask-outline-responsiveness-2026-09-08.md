# Small-image outline responsiveness

The preceding Scene-to-Edit work was verified progress, not completion of the
active RAW/loading/Scene/model/editing objective. This pass follows its measured
foreground stall without reducing image or mask quality.

## Baseline and diagnosis

The prior 3362 ms observation included inactive-window samples. A fresh,
unprofiled, fully active/exposed baseline is retained at
`/private/tmp/scene-mask-profile-bpcbaT`. Its existing mask-stack telemetry records
1442 ms for adding a mask and 1422 ms for masked exposure. The cumulative fields
attribute most time to two `maskOverlayChanged` emissions and their QML reads.

A separate diagnostic run, `/private/tmp/scene-mask-sample-jrC1Ig/app-sample.txt`,
shows foreground `activeMaskOutlineTrace -> activeMaskOutlinePaths` doing
recipe-aware decode, color processing and composite alpha evaluation. The
512-by-512 synchronous admission ceiling was applied to the reduced preview
dimensions: even an 800-by-596 photo could take this slow foreground route.
The sampled run is not the timing baseline.

The bounded performance-loop gates were declared before the optimization:
at most three candidates, each mask action at least 30% faster, whole-proof
elapsed time and peak memory no more than 10% worse, matching foreground state,
unchanged exported images, retained save/undo behavior and the 120-second deadline.
No second optimization hotspot was attempted in this loop.

## Change and required correctness repairs

All recipe-sampled final outlines now use the existing immutable worker,
regardless of raster size. The source-free geometric fast paths, precision,
refinement, final-alpha calculation, newest-pending queue and freshness checks
remain. The duplicate synchronous sampling implementation was removed.

The first candidate passed the timing comparison but failed expanded correctness
checks and was not accepted. Two dependent issues were exposed:

- A cached decode fallback was labeled as cached final output. Trace presentation
  now retains the worker's fallback classification after cache publication.
- Removing canonical masks from the global-color signature left legacy linear
  and radial mirrors. Serialization synthesized masks with fresh IDs, so valid
  workers were continually rejected as stale. Clearing only enabled flags fixed
  neutral cases but not nonzero exposure or geometry. The complete mask/layer
  groups are now reset for that signature; local work revisions still identify
  the actual mask content.

The second native candidate, `/private/tmp/scene-mask-async-final-tciSe8`, timed
out at the stronger final-outline gate and is retained as failure evidence.
The third candidate clears the full legacy group and passes that gate. Existing
geometry tests now wait for worker publication where appropriate; their alpha,
path, simplification and coordinate assertions remain. Cache assertions reflect
the first final result being read from a worker-published cache.

## Accepted comparison

Final native debug-app proof: `/private/tmp/scene-mask-accepted-YI9HYp`.
Both baseline and final runs are active/exposed for all recorded samples.

| Metric | Baseline | Accepted |
| --- | ---: | ---: |
| Add-mask foreground call | 1442 ms | 74 ms |
| Masked-exposure foreground call | 1422 ms | 53 ms |
| Whole proof | 20931 ms | 16256 ms |
| Maximum observation timer gap | 3642 ms | 1565 ms |
| Peak memory footprint | 2031880160 bytes | 2124089408 bytes |

Peak memory increases 4.54%, within the 10% cap. These are individual native
observations, not p95 or input-to-photon measurements. Final outline completion,
real persistent import, neutral pixel/profile identity, masked exposure/P3
autosave and three-step undo all pass. All four output files are byte-identical
to baseline. `scene-edit-mask.png` was visually inspected for the edited photo;
this screenshot does not establish interactive marching-ants or pointer coverage.

The deterministic gate and its three unit tests are:

```sh
python3 -B scripts/perf/test-compare-scene-mask-proof.py
python3 -B scripts/perf/compare-scene-mask-proof.py \
  --baseline /private/tmp/scene-mask-profile-bpcbaT \
  --candidate /private/tmp/scene-mask-accepted-YI9HYp
```

The native receipt now requires a valid completed outline alpha hash, preventing
an export-only pass from hiding an endlessly rejected outline. Comparison output
is retained at `comparison.json` in the final proof directory.

## Functional proof and limits

Six focused tests pass in 33.29 seconds (`/private/tmp/async-outline-final-regressions.log`):
`editor_preview_outline_trace_test`, `editor_mask_sampling_test`,
`mask_worker_coordinator_test`, `active_mask_preview_worker_test`,
`mask_outline_presentation_test`, and `mask_outline_simplification_test`.

New held-worker fixtures cover small linear and radial masks with nonzero local
exposure, temperature, feather and geometry. Getters must report queued work
while the only worker thread is held, then publish exact reference alpha with
no spurious stale rejection. Native comparison, not the cold unit-test getter,
owns the performance acceptance threshold. Existing tests retain color-change
and same-ID restore rejection, zero-density emptiness, crop-aware geometry,
refinement, generated-alpha, brush and cache coverage.

Additional actual Nexus 6 DNG and reference JPEG runs pass in
`/private/tmp/async-outline-real-raw-jpeg.log`: four working spaces (linear sRGB,
Display P3, Rec.2020 and ProPhoto), cropped and uncropped, exact recipe-aware
fill alpha/color and outline alpha hashes, stale-color rejection, zero-density
emptiness, seven inspection modes and same-ID restore protection. The DNG path
asserts that camera-linear decode metadata requires the engine input transform.
These are pipeline integration checks, not independent photographic calibration
or all-camera/all-space coverage.

All proof catalogs are isolated under `/private/tmp`; their owned servers are
stopped after inspection and their data retained. No user catalog was changed.

Later phase inspection attributes the 1565 ms whole-proof gap to the harness's
after-capture AI self-check (1357 ms), not an editing action. The observation-only
maximum was 1466 ms; subsequent queue-read work and remaining stalls are recorded
in `idle-job-queue-responsiveness-2026-09-08.md`. Cold loading, full-resolution foreground
latency, visible interactive outline/stylus behavior and broader camera/color
validation remain open. This is not proof of app-wide realtime, genuine 4D
training, embedded-model readiness or Photoshop-class completion.
