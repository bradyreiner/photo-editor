# Scene Lab: capture-to-trained-scene implementation and evidence

Training-quality follow-up: `gaussian-training-schedule-2026-09-07.md` documents
capture-sized refinement, controlled same-data image improvement and its added
time/memory cost. Fresh integration reaches 22.94 dB / 0.713 SSIM on the 32-view
capture; ghosting and the broader 3D/4D/film quality requirements remain open.

Optical metadata follow-up: `gaussian-capture-optics-2026-09-07.md` records explicit
EXIF reconstruction priors, conservative camera grouping, unchanged prepared
pixels, and a fresh 32-view run. Photographic reconstruction quality remains open.

Status: working static-3D reconstruction/training integration, **not** completion
of the requested 3D/4D novel-view photography and film workspace. No SOTA quality
or realtime viewport claim is supported by this work.

Subsequent checkpoint: native Metal camera viewing and PNG/JPG export are now
implemented and tested; see `gaussian-native-viewport-2026-09-07.md`. The evidence
below records the earlier capture/training checkpoint, and its then-missing
viewer/export items are superseded only to the extent proved in that newer audit.

## Implemented

- A sixth, lazily loaded Scene workspace accepts a capture folder and a new
  project folder. Existing project destinations are rejected. Inputs remain
  read-only; this flow does not write to the catalog or change model stores.
- Production capture preparation hashes each original, decodes with the app's
  image/RAW decoder, and renders through its canonical CPU color pipeline to
  explicitly sRGB display-referred PNG training images. It does not reinterpret
  camera-linear RAW pixels as display RGB. Decode/render and artifact validation
  run on workers, not the UI thread.
- Asynchronous owned COLMAP processes extract/match features, reconstruct
  cameras, select the largest connected model, and undistort its images. At least
  eight and half the input views must register; coverage counts remain visible.
- An owned Brush process trains actual Gaussians on the resulting posed dataset,
  reserves every eighth view for training evaluation, and saves a PLY plus
  held-out evaluation renders. This is not point-cloud conversion.
- Stage receipts are saved atomically. Persistence failure stops the workflow.
  Logs are bounded. Cancellation terminates owned work and retains partial
  project files; it does not automatically resume or replay commands.
- Full artifact validation checks bounded header/layout/count/size, required
  fields, every float for finiteness, nonzero quaternions, complete payload and
  SHA-256. Property order is not assumed. Trained state requires validation.
- Reopening is read-only and executes no persisted command. A trained project
  resolves only its local `trained/scene.ply`, revalidates on a worker and checks
  the saved hash before advertising the artifact. Changed/missing artifacts fail
  closed; the saved project is not silently rewritten.
- The global recovery banner now reserves layout space instead of covering
  workspace controls. Native QA records both scene-load and overlap checks.

Implementation: `gaussian_scene_controller.{h,cpp}`,
`gaussian_capture_preparation.{h,cpp}`, `gaussian_ply_validation.h`,
`qml/workspaces/SceneWorkspace.qml`, and the desktop Main/QML integration.

## Backends and reproducibility

The tested host has COLMAP 3.13.0 at `/opt/homebrew/bin/colmap` and Brush v0.3.0
for Apple Silicon. The app resolves executable overrides
`PHOTO_EDITOR_COLMAP_BINARY` and `PHOTO_EDITOR_BRUSH_BINARY`, then PATH. Invalid
explicit overrides fail instead of silently substituting another binary.
**These dependencies are not yet bundled/provisioned by the app.** The executable
checks are not a backend version/integrity enforcement system.

Brush release archive: `brush-app-aarch64-apple-darwin.tar.xz`, 41,447,012 bytes,
SHA-256 `65b2631398c839be3c1d4d7160fe2326389dec87830aac0710985e6690a1048c`.
The archive was checked before extraction/execution. Executable SHA-256:
`8380ed40cce870025393e1ea0257e0752351c67a409d827b76dc75ec3a999a71`.
The inspected release includes Apache-2.0 license text. This is a selected
published runtime, not a claim that v0.3.0 is the latest research implementation.

Primary references: [Brush](https://github.com/ArthurBrussee/brush),
[v0.3.0 release](https://github.com/ArthurBrussee/brush/releases/tag/v0.3.0),
[COLMAP CLI](https://colmap.github.io/cli.html), and
[COLMAP sample datasets](https://colmap.github.io/datasets.html).
Local CLI flags were checked against installed COLMAP 3.13.0; the online manual
may describe a newer version.

Build and small regression:

```sh
cmake --build build/release-macos --target gaussian_scene_controller_test gaussian_scene_pipeline_driver photo_editor_app -j 4
ctest --test-dir build/release-macos -R '^(gaussian_scene_controller_test|app_shell_controller_test)$' --output-on-failure
```

Real pipeline driver, with both backend variables configured explicitly:

```sh
QT_QPA_PLATFORM=offscreen build/release-macos/tests/gaussian_scene_pipeline_driver CAPTURE_FOLDER NEW_PROJECT_FOLDER 3000 800
```

The driver uses the production controller and capture renderer. Its 50 ms timer
measures event-loop scheduling, **not** viewport frames, input-to-photon latency
or total app responsiveness. It requires a GUI-capable native Metal environment
for the trainer even when the driver's own Qt platform is offscreen.

## Real photographic evidence

Temporary proof root: `/private/tmp/framehouse-gaussian-fresh-hTd5Cb`.
Dataset: the first 32 alphabetically ordered JPGs from COLMAP's public South
Building sample, credited by COLMAP to Christopher Zach. The downloaded dataset
is not bundled or redistributed by this change. Its redistribution rights were
not inferred from the trainer license.

The first complete pipeline run used **fresh** camera reconstruction from those
originals, not the sample's provided camera poses:

- `receipt.json`: trained; 32/32 registered, zero unregistered; 18,944 Gaussians.
- 76,375 ms end to end; 1,527 event-loop ticks, largest gap 64 ms.
- 3,000 training steps, maximum image edge 800; held-out mean PSNR 21.056559 dB,
  SSIM 0.5849426 as reported by Brush.
- `project/trained/scene.ply`: 4,472,334 bytes, 59 float properties per Gaussian;
  SHA-256 `3cccea9c8ac8fe6120a097fa715498f8c145bf34b1cf08d3491b4f2b9eeaf4c5`.
- The held-out `frame_000008.png` was visually compared with the corresponding
  undistorted photograph. Building geometry is recognizable; tree foliage and
  fine details have severe splat artifacts. This is not photographic-quality
  completion. Camera reconstruction used all views; held-out here means excluded
  from Gaussian training, not an independent unseen-scene benchmark.

The final rebuilt controller was then run through the entire fresh pipeline
again. `final-receipt.json` records trained state, 32/32 views, 19,061 Gaussians,
131,056 ms elapsed, 2,621 timer ticks and a 70 ms maximum gap. It persisted
PSNR 21.055374/SSIM 0.585586 and validated the 4,499,946-byte PLY with SHA-256
`d0bb443cb46f6ff19c696200cb186ed4b48c65aade2c3cf6ecb9435f823d6db2`.
This run overlapped the longer quality trainer; the wall-time difference is not
attributed to a controller regression or optimization.

An earlier 200-step backend-only run used supplied camera poses and yielded
PSNR 11.642644/SSIM 0.36404285. Its blurry output is explicitly a smoke test,
not the fresh-reconstruction proof above. A longer 15,000-step quality comparison
completed on the first run's reconstructed dataset, tracked separately in
`training-quality.log`. It reported 393 seconds of training and produced a
validated 380,065-Gaussian, 89,696,891-byte PLY with SHA-256
`7d31d77ee59071eeabaa86b09cac799d495be531a97f816bff8ca4ebc9ed88ce`.

| Evaluation step | Brush held-out PSNR | Brush held-out SSIM |
| --- | --- | --- |
| 5,000 | 22.119432 | 0.63472795 |
| 10,000 | 22.514357 | 0.69298196 |
| 15,000 | 22.231945 | 0.71045434 |

The 15,000-step held-out image was visually inspected. Fine building/foliage
detail improved, but ghosting and substantial foliage artifacts remain. PSNR is
not monotonic; raising the step count is not a sufficient quality strategy.
Both trainers exited normally with code zero. This longer comparison is not a
second capture reconstruction or an independent quality benchmark.

## RAW proof and its boundary

The real-file preparation mode is deliberately separate from reconstruction:

```sh
QT_QPA_PLATFORM=offscreen build/release-macos/tests/gaussian_scene_pipeline_driver --prepare-only REAL_RAW_PATH NEW_PROJECT_FOLDER 800
```

`raw-preparation.json` records the actual 26,283,846-byte Nexus 6 DNG fixture,
`sourceRaw: true`, an 800x593 canonical sRGB PNG, 499 ms elapsed, nine event-loop
ticks, maximum gap 59 ms. The output was visually inspected. Original SHA-256
was independently rechecked after preparation:
`143ab8714d4e8ae0c7cd2d0da9b37efbd00d9b4d5e9068413e1cb53f798044ac`.

This proves actual RAW preparation on **one** fixture, not multiview RAW
reconstruction, full-resolution fidelity, broad camera/profile parity or an
independent colorimetric reference.

## Tests and UI proof

- Focused protocol/artifact/persistence/cancellation/original-preservation and
  app-shell tests pass. Protocol tests use explicit self-exec fake backends;
  they do not replace the real pipeline evidence above.
- SceneWorkspace `qmllint` passes without warnings. Desktop and driver builds
  pass. Targeted `git diff --check` is clean.
- Initial packaged native UI proof at
  `/private/tmp/framehouse-scene-ui-Z1rrUy` loaded the sixth Scene tab and controls.
  It used isolated preferences/catalog/model storage and downloaded no model
  weights. The deliberate unavailable-catalog banner initially covered the
  workspace header, motivating the layout correction; this initial screenshot
  is not the final no-overlap proof.
- Final rebuilt native proof: `/private/tmp/framehouse-scene-final-ui-AeWSP9`.
  `receipt.json` reports `ok: true`, `sceneWorkspaceLoaded: true`,
  `recoveryControlsUnobscured: true`, and the reopened real scene in trained state
  with its freshly checked artifact hash. `startup.png` was visually inspected:
  the recovery notice no longer covers the header/controls, and 32 captures/32
  registered views are visible. The app exited zero and stopped its owned empty
  model runtime. No weights were downloaded or copied. The 1,086 ms maximum
  startup timer gap remains evidence against an app-wide realtime claim. This
  proves native loading/reopen/layout, not manual clicking through the entire
  capture workflow. Reproduce in an unused isolated proof directory:

  ```sh
  build/release-macos/apps/desktop/Framehouse.app/Contents/MacOS/Framehouse --managed-runtime-proof-dir NEW_PROOF_FOLDER --gaussian-workspace-proof --gaussian-project-proof EXISTING_SCENE_FOLDER
  ```

  The final focused test log is `/private/tmp/gaussian-scene-final-tests.log`;
  final app build log is `/private/tmp/gaussian-scene-ui-final-build.log`.
- That launch's log exposed a Scene Loader `active` binding loop: synchronous
  `onLoaded` changed the loaded flag used by the same binding. Deferring that
  flag/transition update removed the warning. The authoritative final launch is
  `/private/tmp/framehouse-scene-loader-ui-DERiKp` (build log:
  `/private/tmp/gaussian-scene-loader-build.log`). It exits zero, records the same
  scene/reopen/no-overlap checks as true, and contains no SceneWorkspace,
  binding-loop, ReferenceError or TypeError log matches. Its screenshot was
  inspected. Largest startup timer gap is 1,203 ms, again not realtime startup.
  Existing StudioInspectorGroup/font warnings outside Scene Lab remain; no
  app-wide zero-warning claim is made.
- The broader `desktop_shell_source_contract_test` fails at the Studio color
  domain/rendering-upgrade disclosure check (`source_contract.inc:618`). That
  condition reads StudioWorkspace source, not the new Scene workspace. It remains
  an unresolved failure and the full suite is not claimed green.

## Required next gates

1. Interactive native Gaussian rendering with correct anisotropic covariance,
   depth ordering, spherical harmonics, camera transforms and alpha; measured
   frame/input latency and memory on representative scenes.
2. Actual arbitrary-camera photo exports and a connected photographic/film
   pipeline, with held-out comparisons, exposure/color contracts and visible
   limits for unseen/occluded surfaces. The current PLY status panel is no viewer.
3. True temporal 4D capture/reconstruction/training and synchronized time/camera
   controls. Animated PLY playback alone does not satisfy this requirement.
4. Real multiview RAW capture, lens/focal metadata/calibration preservation,
   radiometric/exposure alignment, more datasets, coverage/outlier diagnostics,
   and independent quality evaluation. More steps alone are no quality guarantee.
5. Pinned packaged backend provisioning, dependency/license inventory, clear
   runtime/version checks, crash recovery and explicit restart/resume semantics.
   Retaining a partial project is not an optimizer-state resume implementation.
6. Resource budgets for large captures and display resolutions, stage deadlines,
   per-frame preparation progress, durable bounded diagnostics, full disk/source
   changes and owned process-tree shutdown evidence.

The app-wide active goal remains open, including embedded private-model
inference, realtime RAW/color parity and professional editing-tool completeness.
