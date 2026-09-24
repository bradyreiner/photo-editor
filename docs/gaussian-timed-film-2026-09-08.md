# Timed 4D film export

## Delivered workflow

Open a verified `spacetime-4d` artifact project in Scene Lab and choose
**Export 4D film**. Set scene start/end times in seconds from the artifact
interval's beginning, film length (0.5–600 seconds), and 24/25/30/60 fps.
Choose either the current camera or a saved camera path fitted to the film
length. Reverse the time range for reverse motion; equal times freeze the
scene while the camera can still move. Output is a new MP4, up to 4096 pixels
on the long edge, through the existing native Metal/FFmpeg pipeline.

The dialog exposes an existing saved path when available. Saved camera views
and paths can be authored in the same workspace. Fitting a path changes only
the export's copy; it does not rewrite the saved path's timing.

This is rendering/export of a 4D artifact, not photographic capture-to-4D
training. The input preparation/training/quality gap is unchanged.

## Timing, ownership and color

At frame index `i`, movie time is `i / fps`. Camera interpolation samples that
movie time; scene time is the independent affine mapping:

```text
sceneSeconds = startSeconds + (endSeconds - startSeconds)
                              * (i / fps) / nominalDurationSeconds
```

The receipt records absolute scene seconds, nominal duration, mapping version,
frame count/rate, output dimensions, artifact hash and actual camera keyframes.
The endpoint is exclusive: the last frame is sampled before nominal movie end.
With equal start/end the same scene time is used throughout. Frame count is
rounded to the nearest integer; actual duration is `frameCount / fps`, so an
arbitrary requested length can differ by up to half a frame.

- A temporal scene requires an explicit time interval. Static scenes reject a
  supplied temporal mapping. Non-finite or out-of-artifact times are rejected
  before creating encoder output. Forward/reverse/frozen intervals are valid.
- UI acceptance freezes the scene, camera/path and mapping. Subsequent viewport
  scrubbing or camera navigation does not change the accepted film.
- Playback pauses on export acceptance. The film uses its own worker-owned
  renderer; progress and cancellation remain available. Source replacement or
  viewport destruction requests film cancellation, unlike an accepted photo
  export that is allowed to finish after viewport destruction.
- The existing encoder uses H.264 with sRGB transfer, BT.709 primaries/matrix
  and limited-range YUV420. It is lossy, not pixel-identical RGB, HDR or archival
  encoding. The existing rawvideo/libx264 route is unchanged; see the official
  [rawvideo](https://ffmpeg.org/ffmpeg-formats.html#rawvideo) and
  [libx264](https://ffmpeg.org/ffmpeg-codecs.html#libx264_002c-libx264rgb) documentation.
- Existing destinations are not overwritten. Cancellation during frame export
  removes worker-owned temporary output without publishing a movie/receipt.
  Subsequent complete-file publication and ordinary-failure receipt rollback are
  documented in `gaussian-film-publication-2026-09-08.md`. Movie and receipt are
  still not a crash-atomic transactional pair.

## Verification

Proof root: `/private/tmp/gaussian-timed-film-proof-VVk0fN`.

- `matrix-verified/receipt.json`: six encoded movies (fixed/moving cameras ×
  forward/reverse/frozen time), all four frame rates, 95 decoded frames.
  Minimum decoded RGB PSNR against direct native renders at independently
  calculated times is **46.25 dB**, above the declared 35 dB gate. Dimensions,
  frame counts, rates and color tags are checked with FFprobe.
- A wrong-frozen-time negative control scores 24.58–24.77 dB in the four
  changing-time cases, below that gate. Thus a film silently frozen at its start
  cannot pass this fixture. This is not proof of sensitivity to every subframe
  timing error or a real-scene quality evaluation.
- Six invalid temporal intervals/modes, existing-file preservation and active
  cancellation pass. The matrix uses synthetic correlated motion and temporal
  appearance; it is not output from a real 4D trainer.
- `workspace-verified/receipt.json`: malformed settings and concurrent export
  rejection, frozen current-camera/time plan during subsequent edits, saved-path
  export fitted to 0.5 seconds without changing the stored one-second path.
  Existing photo, scrub, cancellation/reopen and teardown checks still pass.
- `native/receipt.json`: the packaged app invokes the real export-dialog button
  and the QML settings/submission function used after destination selection.
  It exports 48 frames at 800×600/24 fps. The OS destination picker itself is
  bypassed by the proof. `timed-film-options.png` was visually inspected.
- Every frame of that actual app-generated MP4 was independently decoded and
  checked: minimum RGB PSNR **48.10 dB**, wrong-time control **24.75 dB**.
  Log: `/private/tmp/gaussian-timed-film-native-parity.log`. First and last decoded
  PNGs were visually inspected and show the synthetic primitives moving.
- All 17 Gaussian CTests pass, including static-film regressions. Final log:
  `/private/tmp/gaussian-timed-film-final-regressions.log`. App and focused builds
  pass; focused `qmllint` reports no diagnostics. A preexisting QString range-loop
  copy warning in `gaussian_viewport_handoff_test.cpp` remains unrelated.

The native proof uses an isolated catalog/preferences/cache profile and a closed
database port; its catalog recovery banner is expected. No user catalog, source
images or model weights were changed. No realtime/foreground-performance claim
is made from this small-scene export proof.

## Remaining goal scope

Genuine capture-to-4D training, trainer/checkpoint convention and license
integration, held-out moving-scene quality, artifact-production/import UX,
large-scene memory and interactive performance remain unfinished. Export plan
preview/persistence, temporal bookmarks, motion blur, HDR/high-bit-depth or
lossless mastering, and stronger final-publication recovery remain future work.
RAW/color fidelity, loading, managed model/search/agent and professional editing
tools retain their independent requirements in `active-app-completion-goal.md`.
