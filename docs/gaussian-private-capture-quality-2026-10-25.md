# Private capture reconstruction audit

The older 64-photo SUV/location capture still has substantial softness and
stretched foreground detail. The public Bonsai acceptance does not establish
quality for this capture. This audit checks the real source photographs and
rebuilds only into new temporary projects; the original project is not replaced.

Evidence root: `/private/tmp/looksy-suv-quality-v1`.

## Verified baseline

All 64 original JPEGs and four original project/artifact/calibration files are
hash-pinned in `original-identities-before.json`. All photographs identify the
same iPhone 17 Pro 2.22 mm ultra-wide lens and 14 mm-equivalent focal setting.
The old reconstruction uses 64 separately estimated cameras. Their pre-
undistortion focal estimates range from 608.22 to 645.95 pixels, about 5.95% of
the median. That variation is a diagnostic, not proof that any particular focal
estimate is wrong; focus/capture processing and reconstruction ambiguity may
also matter.

The legacy capture uses `canonical-decode-and-render-to-srgb-v1`. Preparing all
64 original files with the current source-preserving color policy produces
`capture-source-color-to-srgb-v2`. Source SHA identities and raster dimensions
match. Across all prepared-image pairs, the mean absolute channel difference is
**31.25 codes**, maximum **78**, and mean pairwise PSNR **17.55851 dB**. These
measure a preparation difference, not an improvement in a trained model.

The original model's first stored camera was rendered with the native renderer
and compared with the original trainer's saved prediction. Agreement is
**71.84887 dB**, mean absolute error **0.00425 codes**, p99 zero and maximum two
codes. Both inspected images show the same major softness/streaking. At this
pose, the large defect is already present upstream of native display; this is
not a complete original-rasterizer certification.

Corrected-color references were then undistorted using the original frozen
calibration. Both resulting `cameras.bin` and `images.bin` are byte-identical to
the old training dataset. The original saved predictions at the same eight
internal evaluation views give:

| Reference images for the unchanged old predictions | Mean PSNR | Mean SSIM | Worst PSNR |
| --- | ---: | ---: | ---: |
| Legacy prepared images | 20.61027 dB | 0.604230 | 15.73254 dB |
| Source-preserving images, same calibration | 15.48416 dB | 0.513627 | 13.19383 dB |

The first row reproduces the old trainer's recorded score. The second exposes
the old model's larger discrepancy from preserved source colors. Neither row
demonstrates a reconstruction improvement. Files:
`original-native-vs-trainer.json`, `preparation-color-audit.json` and
`original-photographic-baseline.json`.

## Reconstruction experiments

The first production alignment uses shared estimated intrinsics and strict
matching with the corrected photographs. It takes 312.12 seconds and retains
only **53/64** views in its largest component. It intentionally stops before
training and is rejected for this experiment's full-coverage requirement.
The application admission flag for at least half the views is weaker than this
requirement and is not treated as photographic acceptance. See
`alignment-plan-before.json`, `alignment-receipt.json` and
`strict-alignment-audit.json`.

The second frozen production run keeps shared estimated intrinsics and uses
standard matching. Its largest model registers **64/64** views with one shared
camera, focal 634.19 pixels and radial coefficient -0.001917. A separate small
three-image component is not used; the production controller selects the largest
model. Every prepared PNG is byte-identical to the audited corrected preparation.
See `production-plan-before.json` and `production-alignment-audit.json`.

The second run proceeds to the original 10,000-step/one-million-splat budget:
seed 42, refinement every 64 steps, growth stop 8,000, 2,000 settling steps and
every-eighth internal evaluation. It uses the actual production controller,
preparer, COLMAP and trainer. An external experiment guard observes the exact
owned driver and can stop only its own worker if registration is below 64 before
training. It observes 64 views at undistortion and training. The guard does not
change application admission policy.

## Completed candidate and failed quality acceptance

The full production run finishes successfully in 1,216.64 seconds and produces
exactly one million Gaussians. Model SHA256:
`49eba8f5e9a95542f71e621f631b5ddfcbb0ceb89f508be54fe4f8a13355fbdf`.
The full-coverage guard records 64/64 and stops no worker. All original source
photographs and pinned project files, plus the frozen driver, COLMAP and trainer
binaries, remain unchanged.

All eight expected internal evaluation names, indices 0, 8, ..., 56, are present.
Independent scoring of the saved predictions gives:

| Corrected-source evaluation | Old model, original calibration | Rebuilt model, new calibration | Frozen target |
| --- | ---: | ---: | ---: |
| Mean PSNR | 15.48416 dB | 19.30561 dB | At least 28 dB |
| Mean SSIM | 0.513627 | 0.588564 | At least 0.9 |
| Worst-view PSNR | 13.19383 dB | 14.87833 dB | At least 22 dB |

Every absolute quality gate fails. This model is retained as an experimental
candidate and does not replace the original project or establish reference
quality. The old and new calibrated image domains differ, so the table describes
an integrated reconstruction result rather than a controlled color-only gain.

The candidate's native first-camera render agrees with its trainer prediction
at 70.55035 dB, mean error 0.00573 codes, p99 zero and maximum two codes. The
remaining large streaks and ghosting are also visible in the trainer's output.
Source photographs and predictions at the outdoor ground view and vehicle/person
view were inspected. Color improves visibly, but foreground detail and the
person/hatch region remain poor. Capture consistency and reconstruction/training
behavior need further investigation; the successful job alone does not justify
raising steps or splat budget.

Evidence: `production-receipt.json`, `full-coverage-guard-receipt.json`,
`candidate-photographic-evaluation.json`, `candidate-native-vs-trainer.json` and
`original-identities-after.json`.

These are photographs held out from neural training, while alignment uses all
64 source views. They are not an independent ground-truth camera set. New and
old calibration/undistortion domains can differ, so later score differences
cannot be attributed solely to color correction. Colors, feature extraction,
camera sharing, poses and seed geometry change together in this integrated run.
The measured candidate misses the photographic bar. Neither this limited
evaluation nor the separate public Bonsai result certifies this capture's
general quality or equivalence with the reference video.
