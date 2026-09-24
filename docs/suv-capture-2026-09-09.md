# White SUV capture reconstruction

User-confirmed subject: the white SUV in the newly added Downloads photographs.
The initial reconstruction includes its surroundings; no subject isolation or
person removal is claimed.

Project root:
`/Users/bradyreiner/Pictures/Framehouse Scenes/White SUV 2026-09-09`

- `source/`: 64 verified copies of `IMG_0829.JPG` through `IMG_0892.JPG`.
  Total original bytes: 481,334,466. Every copy was checked against a SHA-256
  computed from its Downloads original. Originals were not moved or edited.
- `scene/`: native Framehouse capture project, with prepared images, camera
  reconstruction, training dataset, `scene.json` and validated trained artifact.
- Native pipeline driver: `build/dev-macos-debug/tests/gaussian_scene_pipeline_driver`.
- COLMAP: installed 3.13.0_3 binary. Brush: existing local v0.3 runtime at
  `/private/tmp/framehouse-gaussian-runtime-1JGerq/brush-app-aarch64-apple-darwin/brush_app`.
  Nothing was downloaded, uploaded or installed for this run.
- Training configuration: 10,000 steps, 1,600-pixel long edge, maximum one million
  splats, seed 42, every eighth registered view reserved for evaluation.

## Alignment evidence

All 64 images register in one connected reconstruction. The undistorted sparse
model contains 17,650 points and 74,809 observations, with mean track length
4.238470 and mean reprojection error 0.565549 pixels. The diagnostic command is
`colmap model_analyzer --path ".../scene/dataset/sparse"` (no `0` subdirectory in
the undistorted dataset). These are camera-alignment metrics, not film fidelity
or final reconstructed-image quality.

EXIF inspection of a representative photo identifies an iPhone 17 Pro ultra-wide
lens, 2.22mm physical / 14mm equivalent. These are initial priors, not measured
calibration. No body serial is supplied, so the existing conservative importer
keeps separate per-frame camera intrinsics. Source images are already processed
JPEGs, not scene-linear RAW data.

## Current state

Training completed at 10,000 steps with exactly 1,000,000 splats. `scene/scene.json`
reports `trained`, not busy, and the PLY is validated. Total pipeline elapsed time
was 1,948,538 ms; Brush reported 1,534 seconds of training. Another unrelated
Brush job was observed and left untouched, so timing includes GPU contention.

- Artifact: `scene/trained/scene.ply`, 236,001,552 bytes.
- Artifact SHA-256:
  `bf78111a56704b93a1f66f797fe58b32f6f04f9d3c67c7455115c1127c9465fc`.
- Trainer held-out evaluation: PSNR 20.61022 dB, SSIM 0.6042293. This is not
  high-fidelity novel-view completion.
- Native neutral previews saved at the project root and visually inspected:
  `front-render.png` (frame 51), `rear-render.png` (frame 21) and
  `side-render.png` (frame 10). Their comparisons against undistorted input
  photographs measure 23.36, 26.41 and 26.67 dB respectively. These are
  training-view comparisons, not held-out reconstruction scores.
- `held-out-render.png` (frame 48) compares the native renderer against Brush's
  own held-out render, not the source photograph: 70.40 dB PSNR, maximum channel
  difference 2/255 and only seven pixels exceeding 1/255. This supports native
  renderer parity; it does not upgrade the reconstruction-quality score.

Actual native viewer proof opens this project at camera 51 and exercises the
Film controls, live orbit and automatic settled photo. It passes with 17–33 ms
film-control feedback, 20 published motion drafts in two seconds and 1,422 ms
settling under contention. See `viewer-film-performance-2026-09-09.md` for the
measurement scope, screenshots, tests and remaining realtime gates.

Visible reconstruction limitations include thin floating streaks, soft white
bodywork and a moving-person ghost in the rear view. Moving people, reflections,
photographer shadows, sparse coverage and unobserved surfaces remain capture
limitations. Film effects and aperture blur do not repair those defects. The
capture is JPEG, not a RAW or temporal 4D validation.
