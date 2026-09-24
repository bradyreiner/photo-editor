# Gaussian photographic fidelity goal

Active user objective: **make the gaussian splat emulator the highest level film
simulation possible for absolute sharp photos**. This goal is not complete.
It includes the user's earlier full-screen realtime camera manipulation,
automatically resolved photographic preview, grain, halation and accurate bokeh.
Do not redefine it as higher export dimensions, larger splat counts, a benchmark
win, or a generic film preset.

Sharpness means faithfully reconstructed in-focus detail and correctly simulated
optical softness elsewhere, not artificially sharpened silhouettes. A capture
cannot prove unseen geometry or recover clipped/spectral information from JPEG.
Photographic equivalence must be demonstrated within stated capture coverage and
camera/film conditions; it cannot be promised for arbitrary unseen viewpoints.

## Required evidence

| Requirement | Evidence needed before completion |
| --- | --- |
| Reconstructed detail and clean edges | Real held-out photographs at target resolution; 100% crops of silhouettes, lettering, texture and glossy surfaces; geometry/floaters checks across a camera path; independent numerical and visual comparisons, not training-view scores alone |
| Capture/color fidelity | Calibrated RAW-to-radiance path with exposure/white-balance/lens handling, static-scene consistency and highlight retention; explicit limits for processed JPG input |
| Physical full-frame optics | Measured scene scale, sensor geometry, focal length, f-number and focus; occlusion-correct aperture integration; lens-specific distortion/vignetting/aberrations/bokeh profiles with real reference photography and convergence tests |
| Film image formation | Calibrated exposure/density response, grain spectrum/correlation and halation, validated against reference scans; deterministic saved shots and correct physical scale across resolutions |
| Final photos | Optical/film photo export now admits 8192 pixels per edge via tiles; representative maximum-size memory stress, photographic preview/export agreement, color-managed outputs and persistent reproducible settings remain required |
| Realtime photographic workspace | Measured motion/input latency, progressive refinement without stale images, cancel/restart, quality adaptation and representative optimized-build resource evidence |

The selected five camera/film targets and detailed emulation contracts remain in
`gaussian-optics-and-film-requirements-2026-09-09.md`. Generic thin-lens and creative
film effects are implemented, but their measured photographic calibration is not.

## Current evidence and next experiment

The existing SUV baseline is preserved at
`/Users/bradyreiner/Pictures/Framehouse Scenes/White SUV 2026-09-09/scene`:
64 JPEGs, 1600-edge training, 10,000 steps, exactly one million splats,
held-out PSNR 20.61022 dB / SSIM 0.6042293. One inspected original is
5712 × 4284. Native-versus-trainer rendering at one held-out pose is 70.40 dB;
the visible reconstruction defects are not fixed by enlarging the output.

First high-detail experiment: new project, 3200-edge canonical inputs,
30,000 training steps, up to two million splats, same seed 42 and every-eighth
evaluation split. This is a quality candidate, not a promoted preset. Compare
named held-out frames and neutral crops before enabling lens/grain/halation.
If camera alignment changes, disclose it rather than calling this a pure
resolution-only comparison. Existing originals and the baseline project must
remain untouched. No external uploads or new model downloads are needed.

Implementation now exposes a bounded 100,000–2,000,000 budget through the native
controller and pipeline driver; the chosen value persists into the trainer
arguments and scene receipt. Existing callers keep the one-million default.
The visible static-capture form offers high-detail defaults and explicit resource
warnings; those defaults are not evidence of higher reconstruction quality.

Host preflight found 48 GiB RAM and roughly 16 GiB free disk, with another user's
Brush job running. Do not stop unrelated work or delete files to make room.
Use a single new candidate, retain headroom and verify the owned process handle
before interpreting a wait as stopped or starting another reconstruction.

## 2026-09-09 high-detail checkpoint

- Controller, schedule and controller-lifetime tests pass: three tests in
  3.96 seconds. Coverage includes the default one-million compatibility path,
  rejection outside 100,000–2,000,000 before project creation, the five-argument
  QML-visible invocation, exact child-process budget, and saved/reopened budget.
- Actual `Main.qml` capture-setup proof passes and is foreground-eligible at
  1600 × 900 and 1024 × 760 logical sizes. Native framebuffer dimensions and
  containment of all training controls are checked. Both screenshots were
  visually inspected; all three budget choices and manual resolution/step
  controls work. This proves configuration/layout, not reconstructed sharpness.
  Receipt: `/private/tmp/framehouse-photo-quality-SkMPtU/setup-ui-v2/receipt.json`.
  The earlier `setup-ui` proof is superseded because it captured stale native
  dimensions after resize; do not use it as compact-layout evidence.
- A single high-detail pipeline is live, launched through the native driver:
  `gaussian_scene_pipeline_driver SOURCE PROJECT 30000 3200 2000000`.
  SOURCE is the baseline project's sibling `source/` folder of verified copies.
  PROJECT is
  `/Users/bradyreiner/Pictures/Framehouse Scenes/White SUV High Detail 2026-09-09`.
  Owned tool session: `29220`. Preparation completed and the live process entered
  `extracting-features`. A prepared frame is verified as 2400 × 3200. No trained
  candidate artifact or quality improvement is claimed yet.
- Resource preflight: system-wide free-memory percentage 76%, approximately
  16 GiB disk free; roughly 16 GiB remains at the latest check. Existing baseline
  occupies about 1.2 GiB including its source copies. Only one new candidate was
  started; the unrelated trainer was left untouched.
- Native test targets were rebuilt, not the production app bundle.
  `git diff --check` passes. No originals, baseline scene, catalogs or model
  weights were replaced. Next: revalidate the live job, inspect alignment and
  training metrics, then compare held-out neutral images at matched resolutions.

## 2026-09-09 tiled-photo checkpoint

- Explicit still-photo export now supports up to 8192 pixels per edge with
  bounded GPU tiles, full-canvas lens/film coordinates and halation overlap.
  Optical/film texture previews and film-video exports retain their 4096 cap.
- Six focused native tests pass in 5.86 seconds, including tiled/full-image
  comparisons within one 8-bit code value, temporal scenes, deterministic grain,
  a 6000 × 4000 optical/grain image and cancellation without partial publication.
  All thirty baseline SUV motion-frame pixel hashes remain exact.
- Actual native viewer proof exported a 4499 × 6000 PNG with lens/grain/halation,
  the real 6000-edge selector, nine tiles and visible progress in 11.896 seconds
  under concurrent training. Full-resolution tile-junction crops show no obvious
  seams, but retain the baseline's streaks and softness. This is export plumbing
  evidence, not sharp reconstruction or physical camera/film equivalence.
  See `gaussian-tiled-photo-export-2026-09-09.md` for receipts and limits.
- The separate high-detail candidate registered all 64 views and entered
  `training-gaussians`; its Brush child is verified live. No trained artifact or
  improvement is claimed yet. The controller emits incremental logs in memory
  but does not persist each log update to `scene.json`; a stale JSON log is not
  evidence of stalled training. The pipeline driver prints state transitions,
  not every in-memory iteration update.
- Only native test targets were rebuilt; the production bundle was not replaced.
  Next fidelity gate: compare held-out neutral renders from the new candidate
  before applying optical softness, grain or halation.

## 2026-09-09 complete baseline held-out check

All eight baseline held-out appearance views now have independent source and
native/trainer comparisons, input hashes and worst-error native-pixel triptychs.
Native/source means are 20.610263 dB / 0.604230 SSIM; native/trainer means are
71.174574 dB / 0.999978 SSIM. Seven frames differ by at most 2/255 from the
trainer; frame 24 reaches 4/255. This localizes the major visual defects to the
reconstruction/capture rather than a materially different native image.
Inspected crops show streaks, softened geometry, lost ground detail and source
shadows missing or smeared in the reconstruction. The separate higher-detail
trainer is still live; no new-scene quality claim is made. See
`gaussian-held-out-quality-2026-09-09.md` for commands, receipts, provenance and
the requirement to compare matched domains before promoting the candidate.

## 2026-09-09 progressive optical-preview checkpoint

Settled optical previews now publish completed GPU images at increasing aperture
sample counts before the final photo, without changing its sample sequence or
final pixels. The bounded latest-only mailbox rejects stale/canceled/hidden/
export-state images. Six native tests pass, including independently rendered
prefix/final equality and publication guards; actual Main.qml SUV proof displays
4- and 8-sample film images before the final 16-sample photo. Thirty motion-frame
hashes remain exact. See `gaussian-progressive-optics-2026-09-09.md` for evidence,
contended timing and memory/quality boundaries. This improves inspection during
refinement, not reconstructed sharpness or physical lens/film calibration.

## 2026-09-09 surface-focus checkpoint

The Focus page now supports asynchronous click-to-focus with image-domain
mapping, opacity-median depth selection and stale-result rejection. Native tests
and actual Main.qml SUV proof pass, including Escape/right-click cancellation
and preservation of all other lens/film settings. The visible focus field and
render were inspected. See `gaussian-focus-picking-2026-09-09.md` for receipts,
timings and the Gaussian-center/scene-unit approximation boundary. This is useful
focus selection, not calibrated physical autofocus. The separate 3200-edge,
30,000-step, two-million-splat candidate is still running; no trained quality
improvement is claimed. Sharp neutral geometry, scene scale/radiance, measured
lens/film profiles and sustained realtime performance remain open.

## 2026-09-09 physical-camera foundation checkpoint

An explicit-scale physical thin-lens converter now maps an oriented full-frame
gate, focal length, f-number and focus distance to renderer cameras, including
finite-focus magnification. Analytical cases and native Metal images verify
focus preservation, defocus size, resolution scaling and equivalent scene units.
This is a kernel/proof milestone, not viewer integration or measured calibration.
Physical-input persistence/UI, camera-relative precision and smooth aperture
sample convergence remain open. See
`gaussian-physical-camera-foundation-2026-09-09.md` for receipts and limitations.

## 2026-09-09 extended bokeh-quality checkpoint

Optional settled quality now reaches 4096 aperture samples, with old defaults
and sequences preserved. A native circular-highlight fixture shows 94.8% less
interior sampling variation than 128 samples, with essentially unchanged total
brightness. Exact progressive/final pixels, cancellation, persistence and film
source reuse pass. Actual Main.qml displays 4/4096 refinement and interrupts to
a navigation draft in 187 ms in a contended run. Six focused tests pass. See
`gaussian-bokeh-quality-2026-09-09.md`; this is not universal bokeh convergence,
reconstructed sharpness, measured lens/film calibration or sustained realtime.

## 2026-09-09 aperture-precision checkpoint

A reproduced native defect blurred focused surfaces at large world translations
because small pupil offsets were rounded inside the view-matrix translation.
Separate camera-plane/world-space offsets fix it: synthetic focused/pinhole error
drops from up to 183/255 to 0, with translated SH and temporal-path pixels exact.
All thirty real SUV pinhole motion hashes remain exact; seven focused tests pass.
See `gaussian-aperture-precision-2026-09-09.md` for receipts and remaining base
transform/source-coordinate precision limits. Full photographic fidelity is
still unproven; this is an optical sharpness defect fix, not a reconstruction
or calibrated camera/film completion claim.

## 2026-09-09 full-resolution diagnostics checkpoint

Independent reconstruction comparisons now accept up to 8192 pixels per edge,
replacing the old 16 MP cap and full-image double buffers with row-buffered SSIM.
All eight real SUV reports and all 24 error crops remain byte-identical to the
old implementation. Dense/streamed equality passes 132 small-size cases; both
24 MP and maximum 67 MP impulse fixtures agree with independent predictions.
The maximum synthetic metric process uses approximately 530 MiB peak RSS. A
real 5712x4284 JPEG also passes full-resolution identity/decode verification.
See `gaussian-full-resolution-diagnostics-2026-09-09.md` for commands and limits.

This removes a measurement bottleneck, not reconstruction softness. No new
quality preset, scene artifact or measured lens/film profile is promoted. The
existing higher-detail trainer was verified live and left undisturbed. Next:
matched-domain held-out evaluation and fixed photographic detail/edge regions;
do not enlarge low-resolution references to claim native 24 MP fidelity.
