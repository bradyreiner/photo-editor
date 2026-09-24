# Scene Lab camera paths and film export

Implemented and exercised on the current macOS/Metal host. This adds actual
camera-motion movies from static Gaussian reconstructions; it is not temporal
4D reconstruction, film-stock emulation, or completion of the broader app goal.

## User workflow

Save two or more camera views, open Camera path, add/order/remove shots, select
seconds per transition and frame rate, then Save path. Scrub or play the path;
Export film produces an MP4 plus `.camera-path.json` provenance. Export progress
and Cancel film remain available while the worker runs. Existing destinations
are rejected. Capture cameras and saved views remain independently usable.

The path persists in the validated `camera-views.json` project sidecar. Adding a
new saved view preserves the existing path. Path updates use the same writer lock
and atomic replacement as camera-view storage; invalid or missing shot references
cannot replace a valid stored path. Limits: 2–128 ordered shots, 0.5–60 seconds per
transition through the API (whole seconds in the current UI), 10 minutes total,
and 24/25/30/60 fps. Consecutive duplicate IDs and incompatible aspect ratios are
rejected. Nonconsecutive repeats are allowed for return paths.

## Camera and encoding contract

Positions and normalized intrinsics interpolate with smoothstep easing to a stop
at each keyframe. Orientation uses normalized, shortest-arc quaternion slerp,
avoiding Euler wrap. Endpoints are exact; intermediate camera bases remain
orthonormal. This is not a spline with independently editable tangents/speed.

Playback uses elapsed wall time and the viewport's existing latest-frame
coalescing. Export freezes an immutable scene and path, uses its own serialized
Metal renderer, and samples camera time at `frameIndex / fps`. Frame count is
rounded from duration × fps; encoded duration is frameCount / fps. Like an ordinary
video timeline, a one-second 24-fps movie samples 0 through 23/24 seconds rather
than adding a 25th endpoint frame or speeding motion up to reach the endpoint.

Film export is asynchronous, with one bounded RGB frame queued to FFmpeg at a
time. QImage row padding is excluded. The output edge is limited to 256–4096 and
dimensions are rounded down to even values for H.264 YUV420. The MP4 uses libx264,
CRF 18, medium preset, and four encoder threads. This is lossy, not an archival
or exact-pixel master format.

RGB is the native renderer's sRGB display-referred output. Conversion explicitly
uses BT.709 limited-range YUV, with BT.709 primaries and sRGB transfer tags. The
initial encoding test caught missing primaries/transfer metadata despite encoder
options; explicit frame tagging fixed it. The frame-property behavior is described
in [FFmpeg's setparams documentation](https://ffmpeg.org/ffmpeg-filters.html#setparams),
and the RGB pipe contract in [rawvideo documentation](https://ffmpeg.org/ffmpeg-formats.html#rawvideo).
These tags do not apply another transfer curve to already sRGB-encoded pixels.

The encoder runs in a temporary directory beside the destination. Cancellation
before publication kills only the owned encoder and removes only its temporary
output; no final movie or receipt is created. A new receipt is created exclusively
before copying the finished MP4 without replacement. If publication fails, any
new receipt is retained and the error says so. This is not an atomic two-file
commit or crash-resumable export. The worker owns its scene/cancellation state and
does not capture the viewport; source changes and viewport destruction request
cancellation. Existing preview/load destructor waits remain.

## Current evidence

- Six focused tests pass: saved views, path math/persistence, film encoding,
  scene controller, Metal rendering, and interaction-proof eligibility.
  Log: `/private/tmp/gaussian-film-final-tests.log`.
- Path coverage includes three keyframes with a return path, exact endpoints,
  midpoint position/intrinsics, shortest-arc wrap, 2,001 valid intermediate bases,
  invalid configuration/reference/aspect rejection, persistence, preservation
  after adding another view, and failed-update preservation.
- Encoding coverage inspects 12 actual frames, dimensions, 24-fps timing, YUV
  format and color tags. It tests no overwrite, absent encoder, cancellation
  before startup, and active cancellation. Final active cancellation stopped
  after one rendered frame in 38 ms, with no final movie or receipt.
- Desktop build passed without compiler warnings/errors; final code build log:
  `/private/tmp/gaussian-film-timestamp-build.log`. `git diff --check` passed.
- Final packaged native proof root:
  `/private/tmp/framehouse-film-verified-pGB6GS/`. It uses a temporary copy of the
  380,065-Gaussian scene, isolated unavailable catalog and empty private model
  store. The native saved-shot workflow, path playback to its endpoint, movie
  export, path unload/reload, configuration popup and midpoint scrub passed.
  `proof/receipt.json` has `ok: true`, `sceneFilmProof: true`, stage 13, 15,129 ms.
  The final workspace and path popup screenshots were visually checked across
  the two native runs; the final workspace is `proof/startup.png`.
- Independent ffprobe inspection confirms 24 frames, 800×596, 24/1 fps, exactly
  one second, YUV420, limited range, BT.709 matrix/primaries and sRGB transfer.
  All 24 decoded movie frames were compared with native renders at the same
  camera timestamps: minimum RGB PSNR 36.1985 dB, mean 37.5074 dB. The check
  requires at least 35 dB for every frame. This is codec/path parity, not an
  independent reconstruction-quality or all-color-space accuracy claim.
  Log: `frame-parity.log` under the proof root. Reproduce with:

  ```sh
  QT_QPA_PLATFORM=offscreen build/release-macos/tests/gaussian_film_export_test \
    /private/tmp/framehouse-film-verified-pGB6GS/project \
    /private/tmp/framehouse-film-verified-pGB6GS/proof/camera-path.mp4
  ```

- The isolated owned model process stopped and port 55882 was independently
  confirmed closed. No weights were installed/copied and no live catalog or
  user originals were changed.

## Still open

Foreground realtime is unverified: the native motion window was inactive, and
the interaction eligibility check correctly fails. Long/4K exports, multi-GPU
and memory-pressure runs, encoder packaging/licensing, cross-player color
management, disk-full/crash recovery and process-teardown stress need more proof.
FFmpeg with libx264 must currently be discoverable on PATH; it is not bundled by
this change. Missing capability produces an explicit error, not a fake movie.

No temporal 4D data, audio, timeline insertion, HDR/high-bit-depth/lossless movie
output, shutter/motion blur, depth-of-field or film-stock/grain/halation workflow
is added here. Photographic reconstruction quality and unobserved surfaces are
unchanged. RAW, embedded-model and editing-tool completion gates remain active.
