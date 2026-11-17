# Source reconstruction: repeated surfaces and denser capture

The photographic-quality task remains open. The new experiments below are
diagnostics, not promoted reconstruction settings. Product photo matching keeps
its existing default and the optional 0.65 ratio; affine DSP-SIFT, planar-link
pruning and bootstrap continuation are not enabled in the app.

## What the sparse photo experiment established

The preceding [32/5 photo experiment](gaussian-photo-alignment-2026-10-25.md)
retains a badly placed camera despite improved shared calibration. A closer
inspection found an accepted pair between opposite-facing photos with almost
all correspondences on the patterned rug. COLMAP classifies that pair as
planar/panoramic. Such links do not by themselves establish reliable 3D camera
geometry. This is evidence of an ambiguous match graph; moving content or errors
in the reference poses have not been established.

Installed COLMAP 3.13 configuration identifiers and estimation behavior were
checked against its versioned source:
[configuration definitions](https://github.com/colmap/colmap/blob/3.13.0/src/colmap/scene/two_view_geometry.h)
and [two-view estimator](https://github.com/colmap/colmap/blob/3.13.0/src/colmap/estimators/two_view_geometry.cc).
The [official FAQ](https://colmap.github.io/faq.html) also describes affine
DSP-SIFT as an option for more discriminative features. That suggestion was
tested rather than assumed to improve this capture.

Every trial below keeps the same 32 training photos and excludes all five
held-out photos. No reference numeric calibration or pose enters reconstruction.
Additional experiments require maximum rotation error ≤3° and maximum center
error ≤0.1 reference-camera RMS radius, as well as the earlier median/calibration
and unchanged-coverage gates. The original failures remain in their reports.

| Trial | Largest registered set /32 | Median rotation | Maximum normalized center error | Outcome |
| --- | ---: | ---: | ---: | --- |
| Affine DSP-SIFT, shared camera, strict matching | 31 | 31.66° | 1.019 | Fails geometry; no product change |
| Remove planar/panoramic verified links | 19 | 0.90° | 0.020 | Clean partial geometry, inadequate coverage |
| Continue from the clean largest component with all links restored | 19 | 0.90° | 0.020 | No additional views recovered |
| Retriangulate full matches in that component, then continue | 30 | 1.81° | 1.618 | Fails coverage and worst-camera checks |

Two earlier continuation trials incorrectly selected the first numbered model
(five views) instead of the largest clean model (19 views). Those outputs are
retained but do not test continuation from the intended seed. The error is
recorded in `/private/tmp/looksy-photo-planar-links-v1/wrong-seed-audit.json`.
Corrected trials resolve the seed from the comparison's selected-model field,
record its camera count and hashes before launch, and verify it is unchanged.
COLMAP's continuation output is written directly into its output folder rather
than a numbered child; the comparator now handles both documented layouts and
rejects a mixture that could indicate stale output.

Evidence:

- `/private/tmp/looksy-photo-feature-quality-v1`: frozen affine-feature plan,
  CLI logs/database/models and negative comparison.
- `/private/tmp/looksy-photo-planar-links-v1`: original copied match database,
  pruning plan, all continuation trials, audit of the mistaken seed, and their
  positive/negative per-camera comparisons. Camera parameter blobs were not
  edited by hand.

Removing planar links globally sacrifices useful coverage. Restoring them did
not meet the combined coverage and geometry gates. Neither behavior is presented
as a repaired capture or used to justify a trained-quality claim.

## Denser source-capture experiment

The 32-photo input was derived from an existing sparse set of reference images.
A new capture test uses 96 more densely spaced source photos from the authors'
[public Mip-NeRF 360 dataset](https://jonbarron.info/mipnerf360/). All 37 existing
named reference photos are excluded from both alignment and training. The 96
training filenames are selected uniformly from the 255 eligible source names,
before reconstruction. This is a new **96/37** experiment, not an isolated
algorithm comparison with the earlier 32/5 set or an official full-training-set
benchmark.

Only selected `bonsai/images_2/*.JPG` members were fetched. Exact HTTP range
responses, the archive ETag and ZIP CRCs were checked. Downloaded source hashes,
member names and the held-out exclusion list persist. No archive calibration,
poses or trained weights were downloaded into the reconstruction input.
Approximately 92.7 MB were fetched, rather than the 12.5 GB whole archive.

The actual production controller uses the already implemented shared estimated
SIMPLE_RADIAL camera and strict matching, at a 1600-pixel maximum edge. It does
not use affine features or planar-link filtering. Reconstruction intentionally
stops before training, then an independent audit checks the frozen requirements:

- At least 90 of 96 photos registered in the largest component.
- Median relative focal error ≤5%.
- Median rotation error ≤2°, maximum ≤3°.
- Median normalized center error ≤0.025, maximum ≤0.1.
- Exact source hashes and source-to-prepared-image mapping; only the declared
  training names in the feature database; no supplied numeric focal prior.

Reference calibration is evaluation-only. The coordinate similarity uses only
registered training-camera centers. The 37 reference photos and poses do not
enter reconstruction or that fit. Any trained-quality comparison is separate;
training is allowed only after all the pose/calibration gates pass.

The plan reserves a 5000-step, 500,000-splat training run with
seed 42 and the production every-eighth internal evaluation split. This is a
separate stage from the external 37-photo evaluation.

Evidence root: `/private/tmp/looksy-bonsai-source-capture-v1`:

- `archive-inventory.json`, `download-plan-before.json` and
  `download-receipt.json` pin the bounded source download and exclusion list.
- `alignment-plan-before.json` pins the input hashes, binary identities,
  controller command and absolute quality gates before reconstruction.
- `audit-tool-amendment-before.json` records additional component-selection
  validation while reconstruction is live, before its audit result. Pose math
  and all thresholds are unchanged.
- `production-96-shared-strict/` is the new production project. Logs and run/audit
  receipts determine its actual current state; the existence of this folder is
  not evidence of success.

## Accepted camera reconstruction

The production run completes in 481.9 seconds and passes all six frozen camera
quality gates. All **96/96 views** belong to one component. Median relative focal
error is **0.0908%**; median/maximum rotation errors are **0.218° / 0.385°**;
median/maximum normalized center errors are **0.00304 / 0.01442**. The 96 feature
names and source/prepared mapping match the frozen training-only set.

This qualifies the camera reconstruction for this denser capture. It does not
establish an isolated algorithm improvement over the different sparse input,
trained photographic detail, native renderer parity or full video parity.
The actual binary identities match the before-run plan; the audit tool matches
the recorded pre-result validation amendment.

`alignment-audit/report.json` retains the per-camera measurements and sparse
file hashes. `heldout-evaluation-cameras/` contains all 37 independent reference
poses transformed into this reconstruction's coordinate system by the inverse
training-camera similarity. No held-out pose or pixel is fitted, and focal
lengths/pixel grids remain unchanged. The mapping tool verifies audit/plan/source
and sparse-model hashes and refuses a failed alignment audit. Analytic inverse
pose, changed-model rejection and component-selection tests pass.

## Training and color-preparation checkpoint

The preplanned training job completed under `training-run/`. Its COLMAP
undistortion stage passes in 6.34 seconds, retains exactly 96 input names, and
leaves every camera pose unchanged within the checked tolerance. The trainer
receives only `training-run/dataset`; evaluation cameras and the 37 external
reference images are outside that input directory.

`training-run/training-plan-before.json` freezes both commands, binary hashes,
the production-sized refinement schedule (96-step refinement interval, growth
stop 4000, 1000 settling steps) and independent neutral-photo targets: mean
PSNR ≥28 dB, mean SSIM ≥0.9 and worst-view PSNR ≥22 dB, plus visual inspection of
full-resolution detail. These are local evaluation targets, not a replacement
for the full reference-workflow requirements. The first model fails all three
targets on the 37 external reference views: mean 17.39477 dB / 0.413328 SSIM,
worst-view 14.67094 dB. Accurate camera geometry alone does not establish sharp
photographic reconstruction.

Investigation found that capture preparation applied the default photographic
develop to already-rendered JPEG/PNG/TIFF inputs, darkening the training images.
Processed images now receive ICC-to-sRGB conversion; RAW retains its neutral
develop. The corrected preparation is pixel-identical to the inspected real
source JPEG. A controlled corrected-color 5000-step retraining has completed,
preserving the accepted source-derived geometry, tracks and poses. Its external
comparison uses the same 37 reference photos, mapped cameras, binaries and
unchanged targets. It improves mean PSNR to 22.02049 dB and mean SSIM to
0.649668, with worst-view PSNR 18.95436 dB. All three absolute quality targets
still fail. A separately planned larger-budget candidate preserves this dataset
and evaluation. Its 15,000-step / one-million-splat result is nearly flat in
mean PSNR (22.07054 dB), worse in mean SSIM (0.645735) and worse in worst-view
PSNR (18.54281 dB). It is not promoted. A fresh corrected-color production
alignment is recorded separately, without supplying reference cameras to
reconstruction. See [color diagnosis, regression and controlled retraining](gaussian-capture-color-2026-10-25.md).

Fresh corrected-color reconstruction subsequently retains 96/96 cameras and
improves median rotation error to 0.09036° and maximum normalized center error
to 0.004784. At the same 5000/500k schedule every external view improves; mean
quality reaches 24.94575 dB / 0.775294 SSIM, with 20.01140 dB worst-view PSNR.
Absolute targets still fail. A full 255-photo production capture is a separate
ongoing test. See [fresh reconstruction and full source coverage](gaussian-fresh-capture-quality-2026-10-25.md).
