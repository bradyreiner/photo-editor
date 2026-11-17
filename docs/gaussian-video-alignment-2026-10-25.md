# Video frame selection: alignment result — 2026-10-25

**The first paired alignment experiment fails the improvement hypothesis.**
The selected-frame set connects 3/160 views, versus 10/160 for uniform sampling.
Both fail the existing 80-view coverage requirement and stop before training.
The selection control remains off by default and is now explicitly experimental.
No alignment or trained-image quality improvement is claimed.

## Fixed inputs and production path

Both sets come from the same 127.151667-second indoor source, at 1600-edge and
160 requested intervals. The previous extraction receipts, all 320 source PNG
hashes, source-video hash, diagnostic driver and COLMAP binary hashes are frozen
before execution in `/private/tmp/looksy-frame-selection-alignment-v1/plan.json`.
No original input or existing trained project is changed.

The diagnostic driver's `--video-replay` route reuses those exact extracted
frames. It still executes production canonical image preparation, the video
controller, CPU feature extraction, sequential guided matching with overlap 20,
and the mapper. It retains the production single estimated SIMPLE_RADIAL camera;
the database confirms one camera in each run. It does not substitute the photo
pipeline's camera grouping or exhaustive matching. COLMAP is 3.13.0 without CUDA,
with four requested threads. Seed defaults remain 0 / Mapper.random_seed=-1;
this single paired observation does not establish repeatability or general cause.

`--alignment-only` collects completed mapper evidence and can cancel before
undistortion/training. In these runs the production coverage check rejects both
sets first. The driver's zero exit status means **alignment evidence collected**,
while each project explicitly retains `state=failed` and its coverage error.
No training schedule or trained artifact is produced.

The hypothesis declared at least 16 additional connected views, no smaller
registered interval span and no longer missing run. The actual view gain is
**-7** and the span shrinks, so the hypothesis fails. Coverage thresholds are
unchanged.

## Observations

| Measurement | Uniform | Selected |
| --- | ---: | ---: |
| Input images | 160 | 160 |
| Estimated cameras | 1 | 1 |
| Detected keypoints | 88,697 | 107,636 |
| Verified image pairs | 93 | 129 |
| Verified correspondences | 11,617 | 17,671 |
| Largest registered component | **10** | **3** |
| Component sizes | 2, 10 | 3 |
| First / last registered interval | 27 / 49 | 47 / 49 |
| Longest missing interval run | 110 | 110 |
| Recovered focal length, pixels | 1313.44 | 1189.79 |
| Required connected views | 80 | 80 |

More local detail, features or verified correspondences did not yield a useful
connected reconstruction here. The result does not isolate whether motion blur,
insufficient overlap, capture motion, scene content, calibration or stochastic
initialization dominates. The selected and uniform reconstructions have different
estimated calibrations and point sets. Their reprojection errors are reported
separately; no cross-pose PSNR/SSIM or trained photographic comparison is made.

`scripts/compare-gaussian-alignment.py` independently reads bounded COLMAP binary
models, checks names, camera references, poses, source-frame immutability and
controller counts, then records registered intervals and calibration differences.
It writes a report and interval CSV into a new absolute directory. Actual models
parse successfully; truncated model input is rejected.

Artifacts:

- `/private/tmp/looksy-frame-selection-alignment-v1/comparison-final/report.json`
- `/private/tmp/looksy-frame-selection-alignment-v1/comparison-final/intervals.csv`
- `/private/tmp/looksy-frame-selection-alignment-v1/uniform-features.json`
- `/private/tmp/looksy-frame-selection-alignment-v1/sharp-features.json`
- Both command files, stdout measurements, stderr stage traces and failed project
  receipts under the same experiment root.

The measurement driver's SHA is recorded before/after both runs in the frozen
plan/wrapper. Its original executable was not retained. Later builds add clearer
coverage guidance and stricter diagnostic receipt/hash checks; final executable
hashes are recorded separately rather than attributed to the earlier measurements.

## Product response and verification

The video setup labels the control **Prefer sharper frames · Experimental** and
explains the alignment-coverage risk. A selected-mode coverage failure also
suggests comparing uniform sampling, alongside denser sampling or a clearer,
shorter continuous section. The default remains uniform. No coverage threshold
is relaxed, and partial training is not automatically authorized.

A controller regression verifies that a selected-mode 3-view result fails,
retains the count, offers the specific guidance and never enters training.
The native capture proof verifies pointer selection, the Create action and
unclipped compact layout with the revised label. Three focused tests pass:
`/private/tmp/looksy-frame-alignment-ctest-final-v1.log`.
Native captures/receipt: `/private/tmp/looksy-frame-alignment-ui-final-v1`.
The desktop bundle builds. A changed-frame diagnostic receipt is rejected before
project creation: `rejection-proof/receipt.json` under the experiment root.

The diagnostic source and comparison script improve reproducibility; neither
is a photographic-quality certificate. Future capture work must address
registration/coverage before spending training time on these tiny components.
Representative denser capture and fixed-domain held-out comparisons remain open.
The sharp imported reference remains available for independent object/lighting
work. Full reference-video capability and quality remain unfinished.

Final identities: `/private/tmp/looksy-frame-alignment-source-binary-final-v1.json`.
