# SUV held-out reconstruction diagnostics

The baseline's visible softness is primarily a reconstruction/capture problem,
not a mismatch between the native viewer and the trainer. This is supported by
all eight held-out appearance views, expanding the earlier single-view parity
check. It is not proof of photographic equivalence or a measured optical model.

## Reproducible diagnostics

`gaussian_image_quality_test` now accepts:

```sh
build/dev-macos-debug/tests/gaussian_image_quality_test --evaluate REFERENCE_DIRECTORY CANDIDATE_DIRECTORY NEW_ABSOLUTE_OUTPUT_DIRECTORY
```

Each named candidate PNG must have an equal-sized reference. The command does
not resize, align or color-transform the images. It computes stored-RGB PSNR,
11 × 11 sigma-1.5 zero-padded SSIM and maximum channel-code error; every frame
and both input SHA-256 hashes are recorded. Aggregate values are unweighted
means across frames, not pixel-weighted means. Infinite identity PSNR is null.

Each frame also saves its three highest-MSE grid regions at native pixel size,
arranged as reference, candidate and absolute error amplified 3×. Grid cores are
256 pixels with smaller border regions; ranking is by mean error, so border
regions may dominate. These diagnostics do not isolate the subject or measure
MTF, and cannot alone determine whether a mismatch comes from geometry, moving
shadows, reflections, exposure, visibility or camera calibration.

The tool accepts 1–256 PNGs and at most 8192 pixels on each axis per pair. Its
original 16 MP admission cap has been replaced with row-buffered SSIM; see
`gaussian-full-resolution-diagnostics-2026-09-09.md` for exact baseline parity,
large-image tests and memory bounds. It refuses an existing
output directory. A failed run may leave partial crops, but does not write a
completed report. It does not infer held-out provenance from filenames or certify
quality: `qualityCertified` stays false. Tests cover known metrics, the independent
SSIM reference, crop ranking/layout, hashes, identity, no-clobber, missing
references and mismatched dimensions. Focused CTest passes in 0.71 seconds;
the target rebuild is warning-clean and `git diff --check` passes.

## Capture provenance and evidence

Baseline project:
`/Users/bradyreiner/Pictures/Framehouse Scenes/White SUV 2026-09-09/scene`.
The current `scene.json` records 64 registered views, seed 42, every-eighth
evaluation split and the validated million-splat artifact SHA-256
`bf78111a56704b93a1f66f797fe58b32f6f04f9d3c67c7455115c1127c9465fc`.
The trainer's `trained/eval_10000/` contains exactly the eight named frames
000000, 000008, 000016, 000024, 000032, 000040, 000048 and 000056.
References are the corresponding undistorted `dataset/images/` PNGs, not the
trainer render. Camera reconstruction used the capture; these are held-out
appearance/training views, not independently withheld camera-calibration data.

Evidence root: `/private/tmp/framehouse-reconstruction-quality-kINkJZ/`.

| Comparison | Mean frame PSNR | Mean frame SSIM | Receipt |
| --- | ---: | ---: | --- |
| Trainer versus source photograph | 20.610268 dB | 0.604230 | `baseline-trainer/report.json` |
| Native versus source photograph | 20.610263 dB | 0.604230 | `native-versus-source/report.json` |
| Native versus trainer | 71.174574 dB | 0.999978 | `native-versus-trainer/report.json` |

All eight native images were rendered anew from the validated PLY and
`dataset/sparse/` camera model, with the named camera, neutral look, zero exposure
adjustment and pinhole/no-film defaults. Native/trainer per-view PSNR ranges from
69.32 to 73.88 dB. Maximum error is 2/255 on seven frames and 4/255 on frame 24;
this is close agreement, not exact identity. Earlier one-view 2/255 evidence must
not be generalized to every view. Native CLI also ran its existing motion checks;
contended timings are not a speed claim. Full PNGs are in `baseline-native/`.

The independent source scores reproduce the trainer's reported 20.61022 dB /
0.6042293 to normal output quantization/implementation tolerance. This confirms
the low fidelity score is real rather than a reference-image mix-up.

## Visual findings and next action

Four trainer/source worst-region triptychs were visually inspected:

- Frame 0, worst region 1: ground detail replaced by stretched translucent streaks.
- Frame 8, worst region 1: softened/deformed vehicle boundary and stretched
  background features; not merely insufficient output dimensions.
- Frame 48, worst region 2: a crisp photographer-shaped source shadow differs
  from the reconstructed smear, with lost ground texture.
- Frame 56, worst region 1: a large dark source shadow is absent from the render.
  This view is the worst whole-frame result, 15.73 dB / 0.394 SSIM.

Shadow inconsistency is an observed mismatch; assigning all of it to motion
requires temporal/source inspection. Do not silently mask these errors out to
raise a quality score. Any future dynamic-object/shadow exclusion needs explicit
mask provenance and separately reported full-frame and static-region scores.

The 3200-edge, 30,000-step, up-to-two-million-splat candidate remains a separate
live training process. No candidate improvement is claimed. When it finishes,
run the same complete named-frame diagnostics before adding film effects. Its
camera reconstruction and undistortion differ from the baseline; native-domain
scores at different resolutions are not a controlled resolution-only comparison.
A matched evaluation domain and fixed subject/silhouette/texture regions are
still required, alongside cleaner geometry, calibrated RAW/radiance, physical
lens/film reference evidence and realtime/progressive rendering.
