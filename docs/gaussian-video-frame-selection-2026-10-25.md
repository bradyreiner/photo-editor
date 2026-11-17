# Gaussian video frame selection — 2026-10-25

**Prefer sharper frames** is an optional control in Scene's video capture setup.
It is off by default and labeled experimental. It compares three nearby candidates per requested time
interval and sends one original decoded frame from every interval to the
existing alignment/training pipeline. It does not sharpen or synthesize images.

The purpose is to avoid taking a blurry frame solely because it landed at a
fixed sampling time. This is one capture-preparation improvement toward the
reference workflow; improved reconstruction quality has not yet been proven.

The first paired production alignment test connects **3/160 views selected**
versus **10/160 uniform**. Both fail the unchanged 80-view coverage check and
stop before training. The improvement hypothesis fails. See the
[alignment experiment and product response](gaussian-video-alignment-2026-10-25.md).

## Evidence guiding the change

Prior indoor checks reproduce Brush's trained outputs closely, while the worst
held-out source view contains motion smear and a large color mismatch. Increasing
training improved the mean but did not fix that worst view. See the reconstruction
section of [the reference comparison](gaussian-reference-parity-2026-10-24.md).

A separate current audit sampled 192 pixels at each of three unchanged Bonsai
poses, using original-opacity rendering against an independent double-precision
interpretation of the published original raster equations. Maximum channel
errors were **2, 1 and 1 codes**, respectively. These samples do not explain the
large visible reconstruction blur and do not establish full raster equivalence.
The oracle covers near culling, projection, covariance filtering, tile admission,
SH and compositing. It does **not execute CUDA**. The paper-era images remain an
unverified-weight comparator. No product renderer convention changed here.

The source conventions are described in the original project's
[forward implementation](https://github.com/graphdeco-inria/diff-gaussian-rasterization/blob/main/cuda_rasterizer/forward.cu)
and [projection/tile helpers](https://github.com/graphdeco-inria/diff-gaussian-rasterization/blob/main/cuda_rasterizer/auxiliary.h).
Audit artifacts: `/private/tmp/looksy-original-raster-audit-v1`.

## Selection and provenance

For N requested frames, the optional analysis pass produces 3N proxies at a
maximum 320-pixel edge. A luminance Laplacian-variance measure ranks each group
of three; ties retain the earliest candidate. The second decoding pass selects
the chosen indices from the same candidate-rate sequence, at the requested
image edge. Output pixels retain the existing tagged SDR Rec.709-to-sRGB
conversion, orientation and no-upscaling rules. Uniform extraction retains its
existing v1 behavior and receipt.

The focus measure is sensitive to texture, noise, exposure changes and moving
subjects. A higher score does not prove less motion blur or better geometry.
One selected frame per interval preserves temporal spread; it does not guarantee
spatial coverage or sufficient overlap. Requested counts above the native frame
rate can still repeat decoded source frames, as in uniform sampling.

The new `framehouse-video-capture-v2` receipt records all 3N proxy scores/hashes,
selected candidate indices, approximate sample times, final PNG hashes, source
hash and exact FFmpeg filter. These times describe the nearest-frame sampling
sequence, **not exact original presentation timestamps**. Proxy images are
removed on success, failure or cancellation; the original video stays untouched.
The additional pass and worst-case proxy storage are included in admission.
Incomplete candidate intervals or selected outputs stop preparation before
alignment. Existing output folders are refused. Cancellation terminates/reaps
the codec process and publishes no successful receipt.

The selected mode crosses the QML Create action, controller worker, saved scene
state and reopen path. No trained model or existing project is replaced.

## Native capture form

The control is video-only, with an inline explanation. Native pointer checks
cover the default, selection, capture-type changes, compact layout and the actual
Create scene action to an injected preparer. The compact proof verifies that the
checkbox and Create action fit inside the popup. Current-scene camera/export rows
are hidden while new-capture setup is open; they previously forced that form wider
than its clipped viewport. Captures were inspected at 1440×900 and 1024×760.

Authoritative retained UI receipt:
`/private/tmp/looksy-sharp-video-ui-final-v2/receipt.json`.
The injected preparer deliberately returns an acknowledgement error before any
alignment/training subprocess starts. Real decoding/selection is tested separately.
An earlier default-temp-path UI check failed because the fixture compared a
noncanonical path to the controller's canonical input. The fixture is corrected;
that failure remains in `/private/tmp/looksy-sharp-video-ctest-final-v1.log`.

## Decoder and controller checks

A lossless synthetic clip contains a known sharp third candidate and distinct
markers in all sixteen intervals. Every selected full-resolution image matches
an independent direct decode of that exact source frame. Tests also check flat
frame tie handling, no upscaling, analytical SDR transfer, counts, hashes,
no-clobber, source preservation, and cancellation after proxy writing has begun.
Cancellation removes the proxy directory and leaves no success receipt.
Controller tests cover both default six-argument and explicit seven-argument
Qt calls, worker forwarding, frozen mode and reopening saved provenance.

The final focused suite passes **6/6 tests** and covers video preparation, controller handoff, native
video-focus UI, existing cleanup UI, rendering/optics and film export:
`/private/tmp/looksy-sharp-video-ctest-final-v3.log`.
The desktop bundle builds with the updated capture UI and controller.

## Real indoor source

The existing 127.151667-second indoor clip was prepared into two new independent
folders at a 1600-pixel edge and 160 requested frames. Both produce all 160 frames
and record the same source SHA-256. The analysis pass evaluates 480 candidates;
it selects a later candidate in **107/160 intervals**. Observed extraction times
are **61.19 s uniform** and **134.33 s selected**, with a desktop build running
concurrently. These are functional observations, not controlled performance gates.

Artifacts are under `/private/tmp/looksy-sharp-video-real-v1`, including the v1
uniform and v2 selected receipts and selected full-resolution frames. The capture
implementation did not change after these runs; later edits add test assertions.
Inspected candidates include viewpoint/texture changes and remaining motion blur.
No registered-pose image metric is computed between different-time frames.
Neither alignment success nor trained-model quality improvement is claimed.

## Remaining qualification

The next quality experiment needs the same clip, frame/compute budget, preserved
full-frame held-out references and declared alignment domains. Compare connected
views, coverage, neutral renders and source-detail crops. Changed camera
calibrations must be reported explicitly. Focus scores alone cannot qualify that
experiment. The option remains off by default pending representative results.

Full reference-video capability and photographic quality remain unfinished.
