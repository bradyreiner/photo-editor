# Timeline scene-camera tracking validation

Status: **runtime, controller/export, and isolated native scene workflow passed on this synthetic fixture.** This is not real-footage or broad quality qualification.

## Fixture

The deterministic fixture is generated outside the repository so it does not dirty the worktree or any shared build tree:

- Video: `/private/tmp/looksy-scene-tracking-proof/scene_tracking_fixture_ffv1.mkv`
- Individual PNG inputs: `/private/tmp/looksy-scene-tracking-proof/frames/frame_000000.png` through `frame_000015.png`
- Camera/intrinsic/scene truth: `/private/tmp/looksy-scene-tracking-proof/ground_truth.json`
- Generator: `/private/tmp/looksy-scene-tracking-proof/generate_fixture.py`
- Video SHA-256: `9ceeb66e00bc0f0a73c3b7b8267957960c5bc70742814079b41d39a0ed622b6c`

It is a 2-second, 16-frame, 640×480, 8 fps lossless FFV1 sequence. Three static, fronto-parallel, individually textured planes lie at depths 4, 7, and 10 metres. A known pinhole camera (620 px focal length, zero distortion) translates laterally by 1.44 metres over the clip and yaws from −1.2° to +1.2°. Frames contain no moving objects. PNG inputs preserve the generated source images without video codec loss; COLMAP must use the recorded filename order.

The generated scene is designed to make multi-depth parallax measurable, not to imitate difficult natural video. Its repeating graphic patterns can also produce feature ambiguity. The far plane is mostly occluded by the near/mid planes from this camera path, so the current solve does not establish recovery of all three depth layers.

## Solve results and metrics

The native `solveTimelineScene` runtime completed with the local `/opt/homebrew/bin/colmap` 3.13.0 binary (“without CUDA”) on the staged 16-frame, 640×480 JPEG sequence derived from the lossless PNGs; this path uses COLMAP CPU feature extraction, sequential matching, and mapping, without Gaussian training. The external result is `/private/tmp/looksy-scene-tracking-proof/scene_result.json`. It registered all 16 frames and reported 1,359 sparse points, mean reprojection error 0.4366 px, median 0.2516 px, maximum 3.0546 px, and median triangulation angle 8.615°. The result uses one shared pinhole camera with `fx=fy=1064.086 px`, zero radial distortion, and a 640×480 image size.

Do not compare raw COLMAP coordinates directly with the generated metre coordinates: monocular reconstruction has arbitrary global scale and coordinate frame. For the checks below, point correspondences were derived by intersecting each reported image observation's known camera ray with the nearest visible truth plane. A robust 7-DoF similarity transform was fit between reconstructed points and these generated truth intersections. The camera check separately fit COLMAP camera centers to the known camera centers; an orientation-only global rotation fit was used to measure per-frame rotation error. This separation matters because the camera and point-cloud fits did not agree on scale.

- registration: 16/16 frames (100%); the runtime result includes 1,359 sparse points;
- reported mean / median / maximum reprojection error: 0.4366 / 0.2516 / 3.0546 px; the native result does not record a reprojection-error p95;
- camera centers after a 7-DoF similarity fit: RMSE 0.00269 m, p95 0.00439 m, maximum 0.00674 m; fitted baseline 1.443 m versus truth 1.445 m;
- per-frame camera rotations after one global orientation alignment: median error 0.369°, p95 0.680°, maximum 0.703°; camera-center RMSE under this orientation fit is 0.0151 m;
- point validation: 1,046 candidate truth correspondences met the filters (point reprojection error ≤1.5 px, track length ≥4, at least three valid visible-plane intersections, and per-point truth-intersection spread ≤0.20 m). An iterative 75th-percentile residual trim retained 705 (67.4%). On these points, 3D RMSE was 0.110 m, median error 0.100 m, and p95 0.176 m;
- two usable depth clusters were recovered: 622 near-plane points at median aligned depth 3.993 m versus 4 m truth (median/p95 point error 0.091/0.168 m), and 83 mid-plane points at 7.106 m versus 7 m truth (0.153/0.188 m). The far plane had no sufficient consistent majority of tracked correspondences and is excluded from the depth-accuracy claim;
- the runtime receipt does not preserve wall time or peak memory, so no performance figure is asserted.

**Important limitation found:** the fixture truth focal length is 620 px, while this solve estimated a shared focal length of 1,064.086 px (about 72% high). A separate camera-center similarity fit had scale 0.1254, while the point-to-truth fit had scale 0.1421, about 13% apart. Thus low reprojection error and accurate camera-center trajectory on this short synthetic shot did not guarantee consistent calibrated focal length or metric depth scale. The feature observations support genuine parallax and two separate recovered depth clusters, but more varied non-planar scenes and camera models must be qualified before making a general 3D placement accuracy claim.

The native runtime succeeded on this fixture. Focused helper tests also cover pre-cancellation, missing runtime, insufficient baseline, incomplete registration, malformed camera geometry, source-time lookup, and plane projection. Controller/export and full native UI checks are recorded below; this clean fixture does not qualify scene cuts or difficult moving tracks on real footage.

The source, generator, ground truth, measured scene result, and measurement scripts are preserved in [`evidence/scene-tracking-2026-10-07`](evidence/scene-tracking-2026-10-07). The metrics above refer to `scene_result.json` from the lossless frame fixture. The controller separately decodes the H.264 `parallax-source.mp4`; its reconstruction may differ because input compression and feature matching affect the result.

## Integrated controller and export

`timeline_tracking_workflow_test` passed with `PHOTO_EDITOR_TEST_SCENE_NATIVE=1`
and the preserved H.264 source. This uses the application decoder and real
COLMAP worker, selects three reconstructed points, attaches an editable title,
changes plane size, checks undo/redo, saves and reopens the timeline, and exports
through the actual FFmpeg timeline filter graph. Both decoded endpoint frames
contain the magenta title; its measured horizontal centroid changes from
273.377 px to 93.551 px. The saved controller solve registers 16/16 cameras,
retains 1,423 points, reports 0.4572 px mean reprojection error, and has a
9.390° median triangulation angle. These are distinct from the lossless-frame
runtime geometry metrics above.

The [editable project](evidence/scene-tracking-2026-10-07/editable-scene-timeline.json),
[export](evidence/scene-tracking-2026-10-07/scene-title-placement.mp4), and
[first](evidence/scene-tracking-2026-10-07/scene-title-0.png)/
[last](evidence/scene-tracking-2026-10-07/scene-title-1.875.png) decoded frames
are preserved. The endpoints were visually inspected. A separate native Cocoa
GPU test verifies projective texture rendering and transparent pixels outside
the tilted quad; its [capture](evidence/scene-tracking-2026-10-07/native-projective-quad.png)
is also preserved. The offscreen software renderer skips this shader test
explicitly rather than claiming GPU rendering coverage.

Focused scene helper, box tracking, SAM2 protocol, foreground matte, tracking
panel, and tracking persistence/export regressions pass. Full native shell
point selection and preview passed separately from these component checks.

The preview publishes the accepted frame's source time with its image URL.
For ordinary native frames it prefers decoded presentation timestamps. When the Qt backend omits them,
it captures the player's source clock at frame arrival, carries it through
queued processing, and marks the published time as estimated. It does not
invent presentation timestamps for the decoded-frame cache.
Scene planes on the tracked Program source map that source timestamp through
the clip's trim and constant playback rate before choosing projected samples.
The frame-scheduling regression verifies that the publication callback sees
the corresponding timestamp, cached frames retain it, and clearing the image
clears the timestamp. A separate injected-sink regression cancels incomplete
first-frame work with a same-position paused seek, emits no new decoder frame,
and verifies recovery from the current sink. Playback and frame-scheduling
tests pass after the recovery change. Source-clock estimates do not establish
exact presentation-time synchronization on backends that omit frame timestamps.

A stricter native pixel comparison found that the installed Qt 6.10.2 Darwin
backend seeks to the 1.0-second image after a 1.875-second request on this
fixture. The sink reports no presentation timestamp. An isolated native Qt
probe reproduced the same result with an all-I H.264 proxy, ruling out a GOP
change as a fix. The exact backend cause is not established. Solved shots now
retain their FFmpeg-decoded analysis frames with explicit original source times;
the preview uses this bounded local frame cache during seeks and playback.
Cached frames pass through the existing color/render pipeline and publish their
source time atomically with the image. Missing cached frames request a new solve.
The original video remains the export source. The native Cocoa shell workflow
passed pointer selection of three points, attaching the title, exact paused
seek to 1.875 seconds, forward playback, and a 0.25-second source trim. Each
frame check compares the processed image against a separate FFmpeg decode of
the original video at the published source time, including an adjacent-frame
comparison. The published scene frame times are not estimated. The
[scene workflow receipt](evidence/scene-tracking-2026-10-07/native-ui/scene-workflow.json)
and [first](evidence/scene-tracking-2026-10-07/native-ui/12d-scene-camera-attached-first-frame-1440x900.png)/
[last](evidence/scene-tracking-2026-10-07/native-ui/12e-scene-camera-attached-last-frame-1440x900.png)
native captures preserve this evidence. Captures were visually inspected.
The final run uses `PHOTO_EDITOR_SMOKE_SCENE_ONLY=1` and explicitly records
`baselineSkipped: true`. The full shell's preceding media-search, ordinary
playback, and editor-readiness checks were intermittent across three final
runs; these remain separate failures, not passed checks. The final scene run
also verifies that the native player is playing while the cached visual frames
advance. Its video-only fixture does not qualify audible output or audio seek
accuracy. The isolated shell has an empty catalog and existing mask-overlay
null-property warnings; this is not a real catalog-session or difficult-footage
qualification. Eight focused tests and the native GPU shader check passed.
The rebuilt app separately launched its Timeline route with isolated data and
settings and shut down successfully; its closed-port catalog helper timeouts
are expected for that startup proof.

## Reproduction notes

The implemented runtime invokes COLMAP `feature_extractor`, `sequential_matcher`,
`mapper`, and `model_converter` with CPU extraction/matching and one shared
`SIMPLE_PINHOLE` camera. Frame times come from the controller's bounded source
sampling schedule. The worker enforces cancellation, timeout, registration,
geometry, and source-identity checks. Its saved reconstruction is independent
of Gaussian training.

Run `timeline_tracking_workflow_test` with
`PHOTO_EDITOR_TEST_SCENE_NATIVE=1`,
`PHOTO_EDITOR_SCENE_VIDEO_FIXTURE` set to the preserved absolute MP4 path,
and `PHOTO_EDITOR_SCENE_PROOF_DIR` set to an output directory. The full shell
workflow additionally uses `QT_QPA_PLATFORM=cocoa`,
`PHOTO_EDITOR_SMOKE_NATIVE_POINTER=1`,
`PHOTO_EDITOR_SMOKE_STOP_AFTER_TIMELINE_WORKFLOW=1`,
`PHOTO_EDITOR_SCENE_TIMELINE_FIXTURE` set to the saved editable timeline,
and `PHOTO_EDITOR_VISUAL_PROOF_DIR` set to an output directory. These native
checks require local FFmpeg/COLMAP and a macOS graphical session.
Add `PHOTO_EDITOR_SMOKE_SCENE_ONLY=1` for the isolated scene workflow; its
receipt is `scene-workflow.json` and explicitly skips the broader baseline.
