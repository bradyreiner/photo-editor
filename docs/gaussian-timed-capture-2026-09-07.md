# Timed capture input for the 4D Gaussian workflow

Status: **validated preparation API and command-line path; not 4D training or
temporal rendering**. Native intake and verified reopening were subsequently
added; see [the workspace implementation and proof](gaussian-timed-workspace-2026-09-07.md).
The broad goal is unchanged.

The current Scene controller trains static 3D with COLMAP/Brush. Animated splat
playback documented by [Brush](https://github.com/ArthurBrussee/brush/blob/main/README.md)
does not turn that integration into a time-aware trainer. A genuine dynamic
representation is a separate requirement; the official
[native-4D Gaussian implementation](https://github.com/fudan-zvg/4d-gaussian-splatting)
models space and time jointly. No upstream 4D training code was copied, installed
or advertised as working here.

## Implemented now

`read_gaussian_timed_capture` validates an explicitly synchronized, fixed-rig
input manifest. `prepare_gaussian_timed_capture` uses the existing canonical
RAW/image decode and CPU color-rendering path to prepare its observations, then
atomically commits `timed-capture.json` only on success.

- Every time has exactly one image from every identified camera; order and
  timestamps remain intact. Files cannot be repeated across observations.
- Explicit seconds/metres and OpenCV world-to-camera conventions are required.
  Matrices are row-major 4x4, affine, with finite right-handed orthonormal rotation.
- Intrinsics are supplied for the upright source pixel grid. The pixel origin is
  the top-left image corner, axes right/down, first pixel center (0.5,0.5).
  Output focal lengths and principal points scale by the actual prepared width
  and height; poses and timestamp values are not modified.
- This first contract requires already-undistorted PINHOLE input and fixed
  per-camera calibration. It validates the supplied numbers and image geometry;
  it does **not** estimate or physically verify calibration/synchronization,
  undistort images, handle rolling shutter, or infer per-frame moving-camera poses.
- Clean relative image paths must resolve inside the manifest folder; missing,
  unreadable, duplicate and linked/escaping paths are rejected. JSON is bounded
  to 16 MiB, cameras to 2–64, observations to 20000, and times to strictly
  increasing nonnegative seconds within one day. Validation checks cancellation
  between cameras/observations.
- Camera dimensions are checked before output is created. Output must be a new
  absolute folder, and image edge must be 256–4096. Inputs remain read-only.
- Original and prepared-image SHA-256 values are stored with each observation;
  the accepted source manifest is hashed too. The canonical prepared PNGs are
  display-referred sRGB, not scene-linear HDR or RAW replacements.
- Cancellation/failure can retain partial generated images, but cannot produce
  the successful final manifest. Existing output folders are never overwritten.
- A folder containing the reserved `capture-4d.json` or prepared
  `timed-capture.json` is explicitly rejected by `startCapture`, before static
  reconstruction/backend execution. Other arbitrary moving-image folders cannot
  be automatically recognized as dynamic by this marker guard.

The output explicitly says `prepared-not-trained` and `trainingAvailable: false`.
It is a dataset artifact, **not** a trained scene. The subsequent workspace
integration lets `Open scene` verify and display it as prepared input without
loading it as a splat scene. Backend export adapters remain unfinished.

## Use

Start from [the manifest example](examples/gaussian-timed-capture-v1.json), replacing
its illustrative calibration and file names with the actual calibrated rig and
synchronized captures. Save it as `capture-4d.json` beside the image folders.
Do not use the example poses as measured calibration.

```sh
QT_QPA_PLATFORM=offscreen build/release-macos/tests/gaussian_scene_pipeline_driver \
  --prepare-4d /absolute/capture/capture-4d.json /absolute/new-dataset 1600
```

The production preparation API runs on the driver's worker. This entry point is
provided through the existing pipeline driver, not a shipped user CLI. Scene Lab
now also exposes **Timed 4D input** with the same preparation API and verified
project reopening. Neither path invokes a trainer, network service or model download.

## Proof

- `gaussian_timed_capture_test`: twenty malformed manifest cases are rejected,
  including convention errors, invalid numeric fields, reflection/nonorthogonal/
  invalid-affine poses, duplicate cameras, nonmonotonic timestamps, missing views,
  repeated files and escaping paths. It also verifies pre-cancellation and wrong
  source dimensions create no output, existing destinations are refused, and the
  static controller returns the specific timed-capture rejection.
- Successful synthetic preparation preserves timestamps, IDs, poses and scaled
  intrinsics; outputs are tagged sRGB and source/prepared hashes are independently
  recomputed by the test. Log: `/private/tmp/gaussian-timed-synthetic-final.log`.
- Real RAW integration uses four temporary copies of the same Nexus 6 DNG with
  **synthetic** two-camera/two-time metadata. It is only a canonical RAW decode/
  artifact test, not real multiview motion or calibrated photographic evidence.
  Receipt: `/private/tmp/gaussian-timed-proof-root-6bf1j5/raw/receipt.json`.
- The actual `--prepare-4d` path processes that manifest at edge 800 in 1199ms,
  with 24 event-loop ticks and a 59ms maximum gap. Output:
  `/private/tmp/gaussian-timed-proof-root-6bf1j5/cli/timed-capture.json`; log:
  `/private/tmp/gaussian-timed-cli.log`. These timer values are not viewport FPS.
- Five focused Gaussian regressions pass in 3.81s: timed input, static controller,
  controller lifetime, camera priors and training schedule. Log:
  `/private/tmp/gaussian-timed-regressions.log`.
- App/driver builds pass without new C++ warnings. The existing 753038-splat
  static project reopens in the native Scene workspace; receipt
  `/private/tmp/gaussian-timed-app-wIKPfG/receipt.json` reports `ok` and
  `sceneWorkspaceLoaded`. Its screenshot was inspected. The unavailable-catalog
  banner belongs to the isolated proof profile. This is static-scene regression
  evidence, not proof of a new 4D UI.

## Remaining 4D completion gates

1. Integrate a genuine time-aware trainer with explicit supported hardware,
   dependency/license/runtime handling and cancellation/recovery. Do not relabel
   independent static frames or animation playback as completed reconstruction.
2. Support temporal primitives/deformations in the native renderer, time seeking,
   novel-view photos and scene-time film export; preserve photographic color.
3. Extend the now-wired calibrated capture intake and prepared-project reopening
   beyond fixed undistorted rigs to the intended camera/video input inventory,
   with correct calibration/synchronization, visual input inspection and recovery.
4. Prove real moving captures with held-out views and times, image quality,
   temporal stability, memory and foreground interaction measurements. A genuine
   calibrated dynamic dataset has been requested from the user; no such evidence
   was supplied or substituted by the synthetic RAW fixture this turn.

All RAW/loading, embedded-model/search/agent and professional editing requirements
outside this preparation step remain active as well.
