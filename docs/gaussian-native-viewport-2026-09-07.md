# Native Gaussian camera viewport and photo export

Status: implemented and exercised on the current macOS/Metal host. This advances
the full capture-to-photograph workflow; it does not complete 4D, film editing,
cross-hardware realtime performance or the fidelity audit.

## Implemented path

`GaussianSceneViewport` loads the verified scene and its undistorted COLMAP
cameras on a worker. The loader checks the PLY hash again while consuming its
bytes, maps properties by name, supports complete SH degrees 0–3, reconstructs
anisotropic covariance from normalized Hamilton quaternions and log scales,
and treats camera IDs as identifiers rather than array offsets. It accepts the
scene's owned artifact/camera directory, not commands from the receipt.

The native renderer performs stable full-float depth ordering with a CPU radix
sort on its worker, then projects and evaluates view-dependent SH color on Metal.
It uses ordered front-to-back float32 framebuffer compositing, an explicitly
display-referred sRGB training/output contract, the selected trainer's covariance
floor and alpha/termination conventions. It is not a point-sprite approximation.

Completed immutable GPU textures are leased to Qt's scene graph; ordinary preview
frames have **no CPU image readback**. There is one render job in flight, newer
camera requests are coalesced, and scene epochs reject obsolete preview results.
The last completed frame remains visible during motion. Hidden viewports do not
keep rendering; viewport resolution follows the fitted image area rather than
the surrounding letterbox, capped at a 1600-pixel edge. Unsupported non-Metal
presentation reports an error instead of claiming a visible frame.

The loaded-scene UI is viewport-focused. It provides capture-camera selection,
orbit, pan, dolly, reset and PNG/JPG photo export. Capture setup can be reopened
using New capture. Exports freeze the scene and camera independently of later
navigation, use the same renderer with explicit readback, embed sRGB, and write a
`.camera.json` provenance sidecar. Existing destinations are rejected and file
creation is exclusive. Partial newly created files are retained and reported on
write failure. Export is asynchronous; it is not an automatic catalog import.

Core files: `gaussian_scene_data.{h,cpp}`, `gaussian_scene_renderer.h`,
`gaussian_scene_renderer_metal.mm`, `gaussian_splat_shader.h`,
`gaussian_scene_viewport.{h,cpp}`, `SceneWorkspace.qml`.

## Real-scene fidelity and standalone timing

Source: the real South Building reconstruction/training described in
`gaussian-scene-pipeline-2026-09-07.md`. The larger scene contains 380,065 Gaussians;
its artifact SHA-256 is
`7d31d77ee59071eeabaa86b09cac799d495be531a97f816bff8ca4ebc9ed88ce`.

The standalone driver rendered its original camera `frame_000008.png` at 800x598
and compared pixels against Brush's saved 15,000-step held-out rendering.

| Renderer attempt | PSNR against Brush rendering | Motion median | Motion p95 |
| --- | --- | --- | --- |
| Initial float16 back-to-front blend | 29.9341 dB | 15.4413 ms | 16.3265 ms |
| Float32 ordered front-to-back blend | 30.5989 dB | 14.6570 ms | 16.0649 ms |

The second attempt's first cold/readback frame took 56.4973 ms, including setup;
its 30 moving-camera frames had a 20.3294 ms maximum. These are serialized worker
render times, not input-to-photon latency. Proofs:

- `/private/tmp/framehouse-gaussian-fresh-hTd5Cb/native-render-f32.json`
- `/private/tmp/framehouse-gaussian-fresh-hTd5Cb/native-render-f32.png`

The approximately 30.6 dB residual above was subsequently traced and fixed; see
the replay below. These historical timings are not post-fix measurements. The
underlying trained scene retains obvious foliage/ghosting artifacts; matching
the trainer's renderer does not establish photographic reconstruction quality.

### Same-scene replay and negative SH fix

The native shader previously allowed negative evaluated spherical-harmonic RGB
to subtract accumulated light, then clamped only the final image. Brush clamps
each evaluated Gaussian color to zero **before** compositing. The native vertex
shader now does the same. It deliberately preserves values above one until final
output: upper-clamping individual Gaussians would incorrectly dim bright,
partially transparent contributions. Analytical native regressions distinguish
both cases (negative foreground over positive background; over-white partial
opacity).

To separate a renderer bug from checkpoint drift, Brush v0.3.0 reloaded the
exported 380,065-Gaussian PLY as `init.ply`. A single evaluation/export step used
learning rates of `1e-30`, no mean noise, and no refinement. This is a bounded
one-step replay, not a zero-step reference API. The read-only
`scripts/compare-gaussian-ply.py` comparison found every position, scale, opacity
and SH float unchanged. Only quaternion normalization changed, by at most
`2.384185791015625e-7`; the files are not byte-identical. There were no nonfinite
pairs. Held-out trainer metrics stayed effectively unchanged (22.231945 to
22.231949 dB; SSIM 0.71045434 to 0.7104545).

The corrected native renderer and trainer evaluated the replay artifact at the
same four camera poses and image sizes:

| Camera frame | RGB8 PSNR | Maximum channel error | Pixels with error >1 | Pixels with error >2 |
| --- | --- | --- | --- | --- |
| 000000 | 70.03995 dB | 5 | 8 | 3 |
| 000008 | 68.43861 dB | 2 | 8 | 0 |
| 000016 | 68.36358 dB | 2 | 9 | 0 |
| 000024 | 69.82193 dB | 4 | 6 | 1 |

The 99th-percentile absolute channel error is zero in every view. An independent
double-precision scalar projection/compositing calculation samples 192 pixels
per view: native-versus-scalar maximum error is one level in all four views;
trainer-versus-scalar maxima are 0, 0, 1, 0. This oracle shares the parsed input
data, so it is not independent validation of the PLY parser. Sparse sampling is
not a replacement for the full-image comparison above. Rare remaining pixel
differences are not called exact parity or assumed explained without analysis.

Evidence root: `/private/tmp/framehouse-gaussian-reference-bf1oZK`:
`parameter-drift.json`, `replay.log`, `replay/eval_15001/`, and
`native-clamped-{000000,000008,000016,000024}.{json,png}`. The earlier
`scalar-audit.json` is a diagnostic from the pre-fix shader/oracle, not the final
reference result. Reproduce native comparisons with:

```sh
QT_QPA_PLATFORM=offscreen build/release-macos/tests/gaussian_scene_render_test REPLAY_PLY CAMERA_DIRECTORY NEW_OUTPUT_PNG CAMERA_NAME TRAINER_REFERENCE_PNG --scalar-audit --no-motion
```

## Native UI, camera motion and exported files

The final app proof uses an isolated catalog/preferences/model store and a
read-only copy of the real larger PLY plus its corresponding cameras. The copy's
minimal receipt is explicitly labeled a viewport proof, not a new reconstruction.
No user originals, live catalog or model weights were changed.

First larger-scene motion proof:
`/private/tmp/framehouse-gaussian-motion-xqTLnq`.
It issued 60 orbit updates, published 59 frames total, and exported both formats.
Its motion timer had a 308 ms maximum gap over 1348 ms. The trace showed unrelated
Studio workspace preloading during this interaction. Prewarm timers now defer
that work while Scene Lab is active; other workspaces retain their preload path.

Final same-scenario proof:
`/private/tmp/framehouse-gaussian-motion-final-aB018H`.

- `receipt.json`: `ok: true`, photo proof stage 4, real scene loaded and displayed,
  recovery controls unobscured, 60 camera-motion ticks in 962 ms, **18 ms maximum
  motion timer gap**, 61 published frames total. This is a focused before/after
  result, not a hardware/corpus-wide latency guarantee.
- The final preview was 902x671: 16.6305 ms worker render, 11.1875 ms GPU,
  4.6884 ms sort/setup, `previewReadback: false`.
- `startup.png` was visually inspected after the camera moved. The native image
  is visible and the loaded-scene controls do not obscure it.
- `novel-view.png` and `novel-view.jpg` are 800x596. macOS `sips` independently
  reports an embedded sRGB profile in both. Their camera sidecars contain equal
  frozen poses/intrinsics and the source PLY hash.
- The PNG is byte-identical to the previous same-camera run, SHA-256
  `1ab94794c71dc8caedd963269437c5fa5caa5cf920ade782df348a391f6c2ee0`.
- The app exits zero and stops its owned empty model runtime. Its **startup**
  timer gap remains 1202 ms; this is not a fix for all app loading stalls.
- The proof invokes the actual viewport's navigation/export methods in the
  packaged QML app. It does not establish exhaustive mouse/stylus/accessibility
  coverage or every file-dialog path.

Reproduce with an unused isolated destination:

```sh
build/release-macos/apps/desktop/Framehouse.app/Contents/MacOS/Framehouse --managed-runtime-proof-dir NEW_PROOF_DIRECTORY --gaussian-workspace-proof --gaussian-project-proof SCENE_DIRECTORY --gaussian-photo-proof
```

The photo flag drives 60 camera updates and writes PNG/JPG into that proof
directory. It must not be used as a normal user-session launch option.

### Post-fidelity-fix native recheck and latency regression

The rebuilt app passes scene display, 60 orbit calls, PNG/JPG export and owned
runtime shutdown in `/private/tmp/framehouse-gaussian-fidelity-ui-VYEX7D`.
The native screenshot was visually inspected. Both exported files are 800x596
and independently report embedded sRGB via `sips`; sidecar camera objects match
(the output filename necessarily differs). The final 902x671 preview reports
17.0404 ms render / 11.6308 ms GPU, without preview readback.

**This run does not pass realtime interaction:** 60 motion ticks took 5956 ms,
with a 1022 ms maximum timer gap. A sampled repeat at
`/private/tmp/framehouse-gaussian-fidelity-repeat-t7BBLJ` reproduced 4945 ms /
1024 ms despite a final 15.8168 ms render / 10.4463 ms GPU. Its `sample.txt`
shows the Qt render thread waiting in `CAMetalLayer nextDrawable` and the main
thread waiting for Qt render synchronization. This identifies the immediate
presentation wait, not its underlying cause; window exposure, drawable lifetime
and the compositor still need investigation. The earlier 18 ms result is not a
repeatable current guarantee. Receipt `ok` describes workflow completion, not a
latency acceptance gate. No live catalog or external model service was changed.

### Presentation investigation: foreground eligibility

Follow-up diagnostics recorded activity/exposure on every motion tick. In
`/private/tmp/framehouse-gaussian-exposure-gLKDQd`, all 60 ticks had an inactive
window despite Qt reporting it exposed; maximum gap was 1018 ms. A normal
Launch Services attempt, `/private/tmp/framehouse-gaussian-foreground-cc0yDq`,
also remained inactive (1021 ms maximum). Read-only macOS session inspection
then confirmed `CGSSessionScreenIsLocked: true` and an on-console session.
The Computer Use runtime could not load, and no attempt was made to unlock or
change system settings. These are not representative foreground measurements.
The exact causal contribution of screen lock still needs an unlocked A/B run;
the immediate Qt/Metal wait was sampled, but no renderer fix is claimed here.

The bounded performance experiment stopped at the environment gate, without
changing graphics backend, sync mode, shader quality or production scheduling.
The scope was the same 60-tick scene interaction, at most three experiments,
with a diagnostic max timer-gap budget of 50 ms and total motion budget 1500 ms.
Export pixels were protected: all four post-SH-fix PNG exports are byte-identical,
SHA-256 `a0a977d82a682042f10757211a3cc52e5f7dc7b57fd9f1743d62fa41da9e5981`.
No performance improvement or memory guard is accepted from an ineligible run.

QA receipts now include `sceneMotionSamples` and `sceneInteractionProof`.
Eligibility requires 60 internally consistent samples, an active/exposed window
and active application throughout. The budget is explicitly UI-thread navigation
timer responsiveness, **not input-to-photon**. The final rebuilt app at
`/private/tmp/framehouse-gaussian-focus-check-3irQEE` completed the export workflow
and measured 961 ms / 18 ms, but correctly reports
`state: unverified-window-inactive`, `eligible: false`, `passed: false`.
This also demonstrates why a single low timer-gap measurement is insufficient.
The new unit test covers missing foreground fields, incomplete/inconsistent
traces and slow eligible runs. All four focused tests pass in
`/private/tmp/gaussian-interaction-proof-tests.log`.

## Focused verification

```sh
cmake --build build/release-macos --target photo_editor_app gaussian_scene_render_test -j 4
ctest --test-dir build/release-macos -R '^(gaussian_scene_controller_test|gaussian_scene_render_test|app_shell_controller_test)$' --output-on-failure
```

All three pass. New native tests cover occlusion order, alpha/termination,
rotated anisotropic footprints, SH color, clipping and orbit geometry. Parser
regressions cover lexicographically reordered SH fields, channel/basis mapping,
non-contiguous COLMAP IDs, missing/truncated camera bytes and hash mismatch.
These small analytical fixtures complement, not replace, the real-scene proof.

Post-fix app/test build: `/private/tmp/gaussian-negative-sh-build.log`;
driver metrics build: `/private/tmp/gaussian-fidelity-metrics-build.log`;
all three focused tests pass in `/private/tmp/gaussian-fidelity-focused-tests.log`.
Earlier SceneWorkspace qmllint remains clean; no QML was edited for the SH fix.
The old broader source-contract binary was stale: after rebuilding it, the
color-domain/rendering-upgrade check passes. The broader test now stops at the
existing visible-control literal-ellipsis contract. No Studio UI or contract was
changed to suppress this failure. The full suite is not claimed green.

## Open requirements

- Broaden the corrected renderer's near-bit agreement beyond this one scene and
  four views; inspect rare residual outliers, grazing angles and independent
  photographic references.
- True temporal 4D reconstruction and playback, film/camera-path editing and
  video output, richer camera/lens controls, saved named views, and agent access.
- Connected film/color edits and higher-dynamic-range output. The current
  display-referred sRGB scene contract is not scene-linear HDR or all color spaces.
- Pinned provisioned COLMAP/Brush dependencies, broad multiview RAW capture,
  radiometric alignment and dependable dataset quality diagnostics.
- More GPUs/displays/scenes, real input-to-photon measurement, full-resolution
  performance, memory pressure, device loss, long interactions and export failure
  stress. Export edges are bounded 256–8192; scenes are capped at two million
  Gaussians. These bounds are not resource-stress proof.
- Full crash/process recovery, export cancellation and interruption verification,
  and direct import into the existing editable/catalog workflow.
- Remaining app-wide RAW/color, loading, embedded-model and professional-tool
  completion gates remain in `active-app-completion-goal.md`.

Convention references:
[COLMAP output format](https://colmap.github.io/format.html),
[Brush projection](https://github.com/ArthurBrussee/brush/blob/v0.3.0/crates/brush-render/src/shaders/project_visible.wgsl),
[Brush rasterization](https://github.com/ArthurBrussee/brush/blob/v0.3.0/crates/brush-render/src/shaders/rasterize.wgsl),
[Brush PLY export](https://github.com/ArthurBrussee/brush/blob/v0.3.0/crates/brush-serde/src/export.rs),
[Qt native Metal textures](https://doc.qt.io/qt-6/qnativeinterface-qsgmetaltexture.html).
