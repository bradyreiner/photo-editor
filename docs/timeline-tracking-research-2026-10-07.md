# Timeline tracking and rotoscoping research (2026-10-07)

## Recommendation

Build tracking as a timeline analysis service with separate backends for **transform tracking** and **mask tracking**. For a first native macOS feature, use Apple Vision `VNTrackObjectRequest` for a selected object's bounding rectangle and `VNTrackRectangleRequest` for a planar surface; keep the latter's four corners so a placed graphic can follow perspective. Both are designed to process a sequence of video frames and return observations that seed the next frame. Run tracking on a worker, record confidence, and expose lost-track/reseed controls. This gives useful tracked placement without claiming pixel-accurate roto.

Rotoscoping needs a distinct temporal matte pipeline. Use Vision's foreground-instance mask as an optional first-frame seed (macOS 14+), then propagate/refine a soft mask over decoded frames and allow correction prompts/keyframes. Vision's current bridge only segments individual `QImage`s; it does not track masks across time. SAM 2 offers an actual promptable video predictor and mask propagation, but its official implementation is a Python/PyTorch stack whose recommended installation is Linux with CUDA; it does not establish Apple Silicon performance. Use it as an optional offline backend and qualify actual Mac behavior separately from upstream CUDA benchmarks. SAM 3 adds text/video tracking but requires a CUDA-compatible GPU (CUDA 12.6+) and its separate SAM License is mutable, so do not select it for a distributable Mac feature without a deliberate license review. CoTracker is useful research for point trajectories, but its repository uses CC BY-NC 4.0 and is unsuitable for a commercial product absent separate permission.

For a lower-risk planar fallback, OpenCV's optical-flow primitives can follow manually selected features and fit an affine/projective transform; require enough inliers and surface failure rather than silently drifting. This is geometry tracking, not semantic object recognition or roto.

## Repository baseline before this implementation

- Apple builds find and link the Vision, Core Graphics, Core Image, Core Video and ImageIO frameworks in `apps/desktop/CMakeLists.txt`.
- `apps/desktop/src/vision_segmentation.mm` implements static-image subject/foreground-instance masks, object-instance masks, and prompt/object selection through Vision. The instance-mask calls are guarded by `@available(macOS 14.0, *)`.
- `vision_segmentation.h` accepts `QImage` and returns one `SegmentationResult`/`ForegroundInstanceMap`; no frame index, timestamp, sequence handler, or temporal state is represented. The repository has no `VNTrackObjectRequest`, `VNTrackRectangleRequest`, or sequence request handler integration.
- Timeline playback receives `QVideoFrame`s, but playback-frame delivery is not a general analysis/export pipeline. The current `analyzeSelectedClipForAutoReframe` is a useful partial: it samples only 3–7 frames per clip, runs independent still-image Vision foreground masks (or luminance fallback), fills missing detections from neighboring samples, smooths the points, and generates editable framing suggestions. That is sparse auto-reframe automation, not continuous object tracking, projective placement tracking, or per-frame rotoscoping.
- SAM2 image encoder/decoder artifacts are recognized and integrity checked by `ai_model_runtime.cpp`. ONNX Runtime can enable the SAM2 **image** selection inference path in configured builds; a status branch reports it unavailable when ORT is not linked. These files are an image encoder/decoder pair, not SAM2's video predictor or temporal-memory implementation. `PHOTO_EDITOR_ONNXRUNTIME_ENABLE_COREML` is an optional provider build switch and is not evidence that a video model runs on Apple GPU.

The existing static cutout and editable mask infrastructure is a good seed/consumer for the feature, but must not be described as video tracking.

## Suggested design

1. **Frame source and job lifecycle.** Add a background frame reader that seeks by presentation timestamp, preserves orientation/color metadata, and produces bounded-resolution analysis frames. Do not run inference in playback callbacks or on the UI thread. Support cancellation, progress, stale-job rejection when clip/source revision changes, and deterministic frame/time mapping for variable-frame-rate inputs.
2. **Track records.** Persist source identity/revision, time base, start/end range, backend/model revision, initial box/quad/points, per-sample transform or matte reference, visibility/confidence, and explicit gaps. Keep transform tracks separate from mask tracks; a bounding-box path cannot stand in for a silhouette.
3. **Tracked placement.** Store a perspective quad for planar tracking and an anchor/pivot for object tracking. Sample/interpolate tracks at the clip's presentation time and apply them in the shared preview and export render path. Provide offset/scale/rotation adjustment on top of the analyzed path, plus a simple disable/retrack path.
4. **Timeline roto.** Seed from a user-chosen frame and object/points, propagate forward and backward, and retain sparse correction keyframes/prompts. Cache generated mattes as content-addressed sidecar data rather than bloating the edit recipe with every raw frame. Bind cache validity to the exact source identity, decode/color settings, model/backend version, time base, and prompt edits. Composite the soft alpha in both preview and export and allow local brush correction.
5. **Failure and proof.** Make confidence/visibility visible; stop or request a correction at occlusion, scene cut, abrupt scale change, or disappearance. Never imply that a mask has been verified because inference returned success. Qualification must measure quality and latency on representative short/long, 1080p/4K, low-light, motion-blurred, occluded, variable-frame-rate, and scene-cut clips on actual supported Macs. Measure tracking drift and matte edge quality as well as analysis time; no cited upstream benchmark demonstrates Looksy-speed performance on Apple Silicon.

## Backend comparison

| Backend | Good fit | Practical limit |
| --- | --- | --- |
| Apple Vision object tracker | Fast native first pass for a user-selected object box and motion-following placement | Tracks a rectangular observation and confidence; it does not return a per-pixel subject matte or guarantee identity through long occlusion/cuts. |
| Apple Vision rectangle tracker | Planar surfaces such as signs, screens, and packaging; four-corner perspective placement | Must seed with a rectangle observation; planar tracking does not cut a moving subject out. |
| Vision foreground-instance request | Native static foreground/instance seed, already available in Looksy | `VNGenerateForegroundInstanceMaskRequest` is image-based, available in this code only on macOS 14+, and has no temporal propagation contract. |
| Optical flow / homography | Lightweight local planar motion or propagation assistance; OpenCV is Apache-2.0 | Feature drift and occlusion accumulate; not semantic tracking or an automatic matte. |
| SAM 2 video predictor | Prompted multi-object video masks with propagation and interaction/refinement; Apache-2.0 code and weights | Official install targets Linux/CUDA and PyTorch; the repository's CUDA-centric requirements and compilation path leave Mac MPS/Metal speed and reliability unqualified. A CPU/converted model path would need its own implementation and benchmark. |
| SAM 3 | Promptable video segmentation/tracking and text concepts | Official prerequisites are PyTorch 2.7+, CUDA 12.6+ and a CUDA GPU; checkpoints require access approval. The distinct SAM License is not Apache-2.0 and terms can change. |
| CoTracker | Dense point trajectories that can help motion analysis | CC BY-NC 4.0 repository license blocks ordinary commercial embedding without separate rights; points do not by themselves create rotoscoping mattes. |

## Official sources

- Apple, [Tracking Multiple Objects or Rectangles in Video](https://developer.apple.com/documentation/Vision/tracking-multiple-objects-or-rectangles-in-video): sequence handler, one observation/request per object, confidence, reseeding, and background-queue guidance.
- Apple, [`VNTrackObjectRequest`](https://developer.apple.com/documentation/vision/vntrackobjectrequest): follows bounding boxes of an initialized observation through multiple images/video frames.
- Apple, [`VNTrackRectangleRequest`](https://developer.apple.com/documentation/vision/vntrackrectanglerequest): initialized rectangle tracking through multiple images/video frames.
- Apple, [`VNGenerateForegroundInstanceMaskRequest`](https://developer.apple.com/documentation/vision/vngenerateforegroundinstancemaskrequest): image-based foreground instance-mask request.
- Meta, [SAM 2 repository](https://github.com/facebookresearch/sam2): video predictor, prompt addition and propagation API; [installation requirements](https://github.com/facebookresearch/sam2/blob/main/INSTALL.md) recommend Linux, PyTorch and CUDA; [license](https://github.com/facebookresearch/sam2/blob/main/LICENSE) is Apache 2.0.
- Meta, [SAM 3 repository](https://github.com/facebookresearch/sam3): video predictor and current CUDA prerequisites; [SAM License](https://github.com/facebookresearch/sam3/blob/main/LICENSE) governs code and model materials.
- Meta, [CoTracker repository](https://github.com/facebookresearch/co-tracker) and [CC BY-NC 4.0 license](https://github.com/facebookresearch/co-tracker/blob/main/LICENSE.md).
- OpenCV, [video tracking / optical-flow API](https://docs.opencv.org/4.x/dc/d6b/group__video__track.html) and [Lucas-Kanade optical-flow tutorial](https://docs.opencv.org/4.x/d7/d8b/tutorial_py_lucas_kanade.html).

The sections above record the research and starting-point audit. The implementation and focused qualification below establish narrower, current behavior.

## Implemented first release

- Apple Vision object-box tracking on an asynchronous streaming frame reader;
  normalized source-time boxes persist in the timeline document. Attachment
  creates ordinary undoable position keyframes while preserving the target offset.
- Independent native foreground masks and optional seeded SAM2 temporal masks,
  persisted as alpha PNG sidecars under the timeline data directory.
- Source identity/range validation, progress/cancel, document-change rejection,
  bounded frames/resolution, and clear lost-track/unavailable/missing-matte errors.
- Source-alpha multiplication in decoded preview and FFmpeg export, before
  retime/geometry. Both raw secondary video layers and primary processed preview
  consume the same sidecars.
- First-frame region selection, source attachment, foreground toggle, and
  configured SAM2 actions in the Motion / Auto Reframe → Track / Cutout panel.

See [local runtime setup and current bounds](optional-sam2-setup.md). The local
SAM2 installation uses upstream commit
`2b90b9f5ceec907a1c18123530e92e794ad901a4`, the SAM2.1 tiny checkpoint, Python
3.12.14, PyTorch 2.5.1, and CPU. A four-frame 410×512 portrait worker test
completed in 4.17 seconds with four nonempty subject masks. This is a short
worker qualification, not sustained real-time, 4K, or difficult-edge evidence.
Planar corner pinning, scene/camera solving, rotation/scale following and
correction prompts on arbitrary video frames are not implemented in this release.

## Multi-point scene tracking with parallax

After Effects' camera-tracker workflow is the right product reference for the
requested multi-point behavior: solve one moving camera and a set of 3D feature
points, then select three or more points defining a best-fit plane and attach
text, a solid, or a null to that plane. Its documented average reprojection
error is a useful quality signal; Adobe also documents that moving objects can
confuse the solve. See Adobe's [3D Camera Tracker workflow](https://helpx.adobe.com/after-effects/desktop/work-with-3d-composition/3d-camera-tracker-effect/tracking-3d-camera-movement.html)
and [effect controls](https://helpx.adobe.com/after-effects/desktop/apply-effects-and-animation-presets/list-of-effects/perspective-effects.html).

This is distinct from fitting an OpenCV homography. A homography estimates a
projective mapping between a plane in one image and another view; it can corner
pin a graphic to one planar surface, but cannot explain different image motion
at different depths. OpenCV describes the homography as a mapping between
planes in its [homography tutorial](https://docs.opencv.org/4.5.1/d9/dab/tutorial_homography.html).
Multiple tracked points are useful only if the system estimates multiple
depths and a shared camera; merely tracking more points and fitting one
homography is still planar tracking.

### Recommended route for Looksy

Add an explicit **Solve Scene Camera** job beside the current point/box
tracking and Gaussian-scene workflows. Reuse the existing source-video
preparation in `gaussian_video_capture.cpp` for bounded, ordered frames and its
time/source provenance, but do not route the job through Gaussian training.
Run a camera solver over those frames, preserve source presentation timestamps,
and save camera intrinsics, per-frame world-to-camera poses, 3D points, image
observations/tracks, reprojection errors, and registration/visibility state in
an editable scene-track sidecar. The current Gaussian controller already
constructs CPU COLMAP feature extraction, sequential matching, and mapping
stages; extract those stages into a training-independent service rather than
requiring `brush_app` just to solve a camera. COLMAP's official [SfM tutorial](https://colmap.github.io/tutorial.html)
describes the same feature extraction → matching/verification → reconstruction
pipeline and specifically recommends sequential matching for video, textured
overlapping views, and camera viewpoints with translation.

For placement, let the user select a best-fit plane from solved points or define
four corners on a chosen surface, then attach an editable 2D text/graphic layer
whose four 3D corners are projected through the solved camera at every source
time. Add placement offset/scale/rotation in the plane's coordinate frame.
This gives true perspective follow and depth-dependent parallax for a planar
graphic. A true volumetric object with scene-accurate occlusion needs geometry
or depth and a 3D compositor; COLMAP's sparse points and camera poses alone do
not provide a complete occlusion matte. Start with planar-attached layers and
make the sparse-scene/occlusion limit visible.

Monocular SfM cannot determine absolute scene scale: its reconstruction is
defined only up to a global similarity transform. A point attach can therefore
follow camera motion, but scale-in-meters, gravity, and collision/occlusion
require a user scale/reference or additional depth/calibration. Pure rotation,
very small baseline, low texture, repeated patterns, rolling shutter, zoom or
focus changes, motion blur, and independently moving objects can make the
solve ambiguous or unstable. Support fixed focal length first, report whether
zoom/intrinsics were solved, show registered-frame fraction and reprojection
error, and allow rejecting moving-object/outlier tracks. Never silently
substitute a planar homography when the requested camera solve fails.

### Reusable code and local runtime audit

- `gaussian_video_capture.cpp` already probes video metadata, extracts
  uniform-time frames with FFmpeg, records source hash/sample times, and bounds
  requests to 16–1920 frames. For scene tracking, preserve actual presentation
  timestamps rather than assuming its current uniform-rate samples are exact
  source frame times.
- `GaussianSceneController` already composes the CPU COLMAP extraction,
  sequential matching, and mapper commands, but currently requires the Brush
  trainer and continues into static Gaussian training. The camera solve should
  call COLMAP without that trainer dependency.
- `gaussian_scene_data.cpp` reads sparse camera/image records for the Gaussian
  viewer but does not expose all `points3D` tracks as a scene-tracking model.
  Extend the parser or consume COLMAP text export to retain point IDs and
  image observations, as described by the official [COLMAP output format](https://colmap.github.io/format.html).
- The current Gaussian camera-path code represents manually authored viewer
  paths, not a solved source-video camera. The splat renderer is a potential
  display building block, but it is not a general projective text/graphic
  compositor.
- The current checkout has `/opt/homebrew/bin/colmap` 3.13.0 available and
  `/opt/homebrew/bin/ffmpeg`; the Homebrew COLMAP build is CPU-only. There is no
  OpenCV or COLMAP link in the desktop CMake target, so COLMAP currently runs as
  an external process and is not a bundled runtime guarantee. COLMAP is BSD-3
  licensed ([license](https://github.com/colmap/colmap/blob/main/LICENSE));
  audit its separately licensed dependencies and binary distribution before
  bundling. CPU mode is suitable for a cancellable offline solve, not a
  playback-rate promise.

### Deterministic parallax fixture and validation status

`/private/tmp/looksy-scene-tracking-proof/` contains a generated 16-frame,
640×480, 8 fps lossless FFV1 sequence, the individual source frames, and
`ground_truth.json`. The static scene has three distinct textured planes at
4, 7, and 10 metres, while the known pinhole camera translates laterally by
1.44 metres and yaws slightly. There are no moving objects. The fixture is
purpose-built for checking that a solve recovers a shared moving camera and
multi-depth points; its deliberately clean synthetic imagery does not qualify
real-world tracking quality. A comparison against the supplied truth must
first align COLMAP's arbitrary world scale/orientation by a similarity
transform, then measure camera/point error and image reprojection error.
The native camera runtime now registers all 16 fixture frames and recovers two
usable depth groups. Focal-length accuracy and broader footage quality remain
unqualified; see [scene-tracking validation](timeline-scene-tracking-validation.md)
and the [implemented workflow](timeline-scene-tracking.md).
