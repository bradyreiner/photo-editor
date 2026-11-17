# Photo calibration and ambiguous-match controls

Scene options → Photos → Advanced now offers two independent, experimental
options, both off by default:

- **Same camera and zoom** uses one estimated SIMPLE_RADIAL camera, without a
  numeric focal prior. The user must confirm an unchanged physical camera, lens,
  zoom, crop and stabilization. Preparation rejects contradictory known camera,
  lens or focal metadata, mixed source formats, and different source or prepared
  image dimensions. Missing EXIF is not treated as proof of shared optics.
- **Reject ambiguous matches** uses a 0.65 SIFT descriptor ratio rather than
  COLMAP's default 0.8. Exhaustive matching, CPU thread limits and mapper settings
  otherwise stay unchanged. Guided matching remains disabled for photos. The
  option can reduce connected-view coverage; the failure message suggests
  comparing the ordinary mode. The existing coverage gate is unchanged.

The choices persist in the project and are frozen before preparation starts.
`startCapture` retains its existing signature and behavior; the new explicit
`startPhotoCapture` entry point receives both choices. Video/timed input keeps
its existing calibration/matching behavior. Effective shared calibration is
recorded as user-asserted and estimated, with the observed EXIF groups retained
separately. These controls are not a qualified reconstruction-quality preset.

## Automatic EXIF grouping correction

An existing defect included `captureTime` in the camera-group identity. Even
fully identified consecutive exposures therefore became independent cameras.
The group hash now excludes that photograph-specific field. Body/lens identity,
focal settings, prepared dimensions and source format still separate groups;
missing camera/lens identification still retains per-image calibration.

A new regression test fails before this fix and passes after it. This repairs
the grouping contract; no reconstruction-quality gain from this correction is
claimed for the PNG experiment below, which has no EXIF focal priors.

## Frozen photo experiment

The existing local public Bonsai reference package supplies 37 sharp PNGs.
Sorted indices 4, 12, 20, 28 and 36 are held out, leaving 32 photos for alignment.
This is a new 32/5 experiment, not the dataset's official training/test split.
All alignments use identical 1559×1039 prepared PNGs and exact SIFT features.
No reference focal length, camera pose or trained Gaussian weights enter
reconstruction. COLMAP 3.13 runs locally on CPU with four threads.

Reference camera calibration is used only for evaluation. A proper similarity
is fitted using registered training-camera centers; held-out poses are not used
for that fit. Rotation errors use that same gauge. The original acceptance plan
requires at least 29 registrations, no coverage loss relative to the baseline,
at least 25% common-view median focal-error improvement when the baseline already
registers 29 views, candidate median focal error ≤5% and median rotation error
≤2°. Those thresholds were not relaxed after observing failures.

| Alignment | Registered /32 | Median focal error | Median rotation error | Median normalized center error | All declared gates |
| --- | ---: | ---: | ---: | ---: | --- |
| Production baseline, per-image cameras | 31 | 13.77% | 9.79° | 0.578 | No |
| Shared camera, ordinary matching | 32 | 1.09% | 34.11° | 0.460 | No |
| Shared camera, guided matching | 32 | 0.66% | 33.78° | 0.459 | No |
| Independent cameras, ratio 0.65 | 31 | 3.94% | 4.42° | 0.062 | No |
| Shared camera, ratio 0.65, direct CLI | 31 | 1.11% | 1.67° | 0.032 | Yes, this run only |
| Shared camera, ratio 0.65, production controller | 31 | 0.90% | 2.54° | 0.036 | **No** |
| Shared camera, ratio 0.65 plus guided matching | 31 | 1.46% | 33.43° | 0.448 | No |
| Production feature/match DB, tighter 4 px absolute-pose threshold | 31 | 0.90% | 2.54° | 0.036 | No |

Center error is divided by the reference training-camera RMS radius. Medians
do not establish clean geometry: the production candidate's worst center error
is 0.757 radius. That view has 67 triangulated observations, just 34 supported
by tracks of at least three views, and 1.21 px median point reprojection error.
Other views can have low support while being reasonably placed, so a simple
point-count cutoff is not a demonstrated repair.

The direct CLI run passes the declared gates but retains two clear position
outliers. The production run misses the pose target. This establishes improved
calibration and much lower typical geometry error, **not repeatable acceptance**.
No new Gaussian model was trained and no held-out photographic-quality gain is
claimed. Training remains gated on resolving or qualifying the pose problem.
The additional guided and tighter-pose hypotheses failed and are not product
defaults. The private indoor video is a different capture; no improvement to it
is established here.

## Independent convention check and evidence

All 32 parsed binary poses agree with COLMAP's official text export. COLMAP's
own `model_aligner` reproduces the comparator's transformed camera centers to
3.56e-15 maximum coordinate difference. The bad result is not explained by a
quaternion-order or similarity-direction error. The convention follows the
[COLMAP output specification](https://colmap.github.io/format.html): Hamilton
world-to-camera quaternions and camera center `-Rᵀt`.

The comparison tool checks source hashes, production source-to-prepared-name
mapping, exact training-only image sets and feature sets, calibration and binary
bounds. Self-tests cover non-axis-aligned similarity, independent rotated pose
conventions, degeneracy, truncation and incorrect photo/camera mapping.

Evidence root: `/private/tmp/looksy-photo-intrinsics-experiment-v1`:

- `plan.json` freezes source hashes, the 32/5 split and original gates.
- `baseline/`, `shared-cli/`, `shared-guided-cli/`, `shared-strict-cli/`,
  `independent-strict-cli/`, `production-shared-strict/`,
  `shared-strict-guided-cli/` and `shared-strict-pose4-cli/` retain databases,
  sparse models, commands and logs. Every adaptive experiment has a before-run
  plan; camera blobs were not edited by hand.
- `comparison-*/report.json` retain every positive and negative result.
- `pose-audit-v1/` contains the independent official alignment and a pairwise
  epipolar diagnostic. Some accepted match sets disagree strongly with the
  reference geometry, motivating the stricter-matching experiment. This is an
  evaluation diagnostic, not an automatic product filter.
- `production-before.json` pins the controller driver and relevant source
  identities for the real production run. `production.stdout.json` records both
  options, 31 registered views, the matching strategy, and intentional stopping
  before training. Subsequent changes fix timestamp grouping and improve the
  low-coverage message; the measured PNG calibration contract is unchanged.

The diagnostic driver accepts:

```sh
build/dev-macos-debug/tests/gaussian_scene_pipeline_driver \
  SOURCE_FOLDER NEW_PROJECT 200 1600 500000 \
  --same-camera-and-zoom --strict-matching --alignment-only
```

An alignment-only zero exit means evidence was collected, not that the quality
gate passed or a trained scene exists. Inspect the comparison and receipts.

## Native workflow verification

The native SceneWorkspace proof checks both default-off choices, actual pointer
toggles, photo-only visibility, retained choices across capture-type switches,
compact scrolling, and the actual Create action reaching an injected preparer.
The preparer intentionally stops before alignment. Desktop and 1024×760 logical
captures were inspected; both options and Create remain accessible:
`/private/tmp/looksy-photo-capture-options-ui-v1`.

Controller tests verify the old QML-visible invocation, each new choice
independently and together, exact child-process flags, persistence/reopen,
eleven metadata/dimension contradictions and low-coverage training prevention.
Real decoding/alignment is separately measured above. Full end-to-end trained
photographic quality, weak-view repair and reference-video parity remain open.

The desktop bundle builds. The final focused suite passes **10/10 checks** with
no skips, including scene/controller lifetime, camera priors, timed/video capture,
training schedule, comparison-tool self-tests, existing cleanup/video controls
and the new photo controls. Log:
`/private/tmp/looksy-photo-capture-options-ctest-final-v1.log`.
