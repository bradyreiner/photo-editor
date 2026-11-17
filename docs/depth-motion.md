# 3D Motion

Scene opens directly into a workstation, with a large viewport, loading actions
in its toolbar, and an inspector on the right. **3D Motion** turns an image or video into a
colored, depth-estimated point cloud. The **Point cloud** view is interactive:
Play advances the video's XYZRGB frames while the camera remains under your
control. It uses the same camera toolbar and input components as reconstructed
scenes: Look, Orbit, Pan, Roll, Target and Reset. Drag in the selected mode,
right-drag or Shift-drag to pan, and scroll to move closer or farther away.
Double-click picks an orbit target without moving the camera. WASD/QE moves
relative to the camera; Alt-left/right rolls. Space toggles playback.
Scrubbing changes video time without resetting the camera.

The live look controls include:

- **Z exaggeration**, from 0× (flat) through 1× (estimated depth) to 8×.
- Point size from 1–8 pixels and stable point density from 5–100%.
- Round points, square pixels, or soft dots.
- Source color, monochrome, or a depth color treatment; point brightness.
- Six depth-gradient palettes (Spectrum, Glacier, Ember, Aurora, Ocean and Silver),
  with editable Near, Mid and Far colors. Gradient colors save with the scene.
- Black, charcoal, or white backgrounds.

These controls work while playing and orbiting, without rerunning depth inference.
Camera pose, playback position and point appearance save with the project and
carry over when rebuilding the same source. Reset resets the camera while
preserving the point look. The compact inspector keeps Appearance visible and
discloses Build and Export settings; narrow windows use a settings drawer.

**Save scene** gives either a reconstructed scene or a 3D Motion project a name
and adds its viewport thumbnail to **Library → Scenes**. **Open in Scene** restores
the project and saved camera view. The library references the existing project
and its media/cache; it does not copy those files. Missing projects remain listed
as unavailable. The scene library is stored in the app data directory's `scenes`
folder; `PHOTO_EDITOR_SCENE_DOCUMENTS_PATH` overrides that folder.

**Record** captures the scene viewport while you orbit, look, pan, roll, zoom,
play or pause. **Stop** finishes a silent H.264 MP4 at 24 fps, with a maximum
edge of 1280 pixels and the viewport's aspect ratio. The recording includes the
displayed point styling and colors, with no editor controls over the image.
It saves automatically to the originating scene project's `recordings` folder;
**Video saved** opens the result. Reconstructed scenes use the same recording
control. Capture writes timestamped temporary PNG frames in the background,
then encodes after stopping; missed capture intervals hold the previous image
to preserve elapsed time. Only a completed video is published, with a unique
filename. Cancellation or encoding failure preserves earlier recordings.
Changing projects or leaving the workstation stops and saves the active take.
Recording stops automatically after ten minutes.

**Depth video** in the preview selector plays and scrubs the project's actual
per-frame depth maps, with white indicating nearer surfaces. It shares the
point-cloud source clock, so switching between the two retains source time.
New projects prepare these grayscale PNG previews alongside their point cloud.
Older projects prepare them on first use from the existing floating-point depth
cache, without running inference again or changing the original recipe.

Build settings control depth estimation, the convergence plane, duration, source
start, resolution, and depth inversion. The local
[Depth Anything V2 Small model](https://huggingface.co/depth-anything/Depth-Anything-V2-Small-hf)
estimates depth for every decoded video frame. Depth scale smoothing and
flow-guided blending reduce flicker, with a reset at scene cuts. This is a
per-frame model with temporal guidance, rather than a temporally trained video
model. Images/video are processed locally; setup downloads the model and Python
packages, and requires Python 3.10+, FFmpeg, and FFprobe.

Pixels are back-projected through an assumed 50° horizontal pinhole camera into
relative XYZ coordinates. Each live frame has its own XYZ and original RGB data.
The native viewer projects those points through the current orbit camera and
depth-tests their coverage; it does not shift a flat depth image. Z exaggeration
scales the points' actual Z coordinates around the convergence plane. The live
view uses a stable source-pixel sample grid, capped at about 180,000 points per
frame, and a bounded rendering worker. It shows gaps instead of filling unseen
surfaces. Side and rear views therefore expose the limits of a single view's
estimated depth.

**Rendered clip** retains the preset slide/arc/push camera renderer. **Export
rendered clip** saves that MP4 with source audio where present; it does not record
manual navigation in the interactive view. The photographic preset can fill small
holes, while its point style leaves gaps. PLY and 16-bit depth exports represent
the first source frame. They retain the estimated geometry, rather than the live
camera/look treatment. Use **Record** for a video of manual camera navigation.

Projects save under the local depth-motion projects folder. The JSON recipe
references a cache of original frame RGB and floating-point depth. A
`pointcloud/manifest.json` and numbered `.xyzrgb` files supply the interactive
view; records contain little-endian float32 XYZ followed by three RGB bytes.
Existing projects can prepare this sequence from their saved depth automatically,
without another inference pass. Early caches without a manifest are recovered
from complete numbered frames. A missing or incomplete frame reports an error.
Each new build gets a separate folder, and cancellation preserves the previous
completed project. Existing export destinations are preserved.

Shots currently cover 1–12 seconds, with 24 fps in the UI and maximum source edges
of 640/960/1280/1920 pixels. The output is SDR RGB. Monocular depth has no metric
scale, hidden geometry, or camera tracking; fast motion, fine structures,
transparency, and occlusion changes can still produce artifacts. The XYZ sequence
also requires local storage (approximately 534 MiB for the qualified 12-second,
960×540, 129,600-point example).

For checkout development, tools live under `.photo-editor/depth-motion` when
started from the checkout. Otherwise they use the app's local data directory.
`PHOTO_EDITOR_DEPTH_MOTION_HOME` changes that folder;
`PHOTO_EDITOR_DEPTH_MOTION_PYTHON` selects a compatible Python executable.
The Python worker is bundled with the desktop build.

Earlier preset-render evidence is in
[depth-motion-2026-11-02](evidence/depth-motion-2026-11-02/verification.json).
The interactive point-cloud tests additionally cover front/back occlusion,
live Z deformation and point styling, actual pointer orbit during playback,
project/view persistence, and real-video frame advancement.
