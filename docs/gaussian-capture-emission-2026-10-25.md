# Object emission received by captured colors — 2026-10-25

This records the original v14 cosine estimator and its qualification. The later
[targeted-emitter sampler](gaussian-emission-sampling-2026-10-25.md) adds a
versioned alternative and becomes the default when newly enabling reception;
existing frozen v14 recipes retain the behavior documented below.

**Scene composition → Captured → Receive object emission** lets glowing authored
objects illuminate the captured scan when **Light captured colors** is selected.
The option is off by default. Sphere/box emission and imported OBJ material `Ke`
are supported. The light-sample control is also available on this page while
emission reception is enabled. Navigation uses eight samples; final stills and
film use the selected 16–1024 samples.

This extends the existing captured-color relighting approximation. It does not
recover unlit albedo, remove baked illumination, reconstruct physical materials
or establish Octane equivalence. Small sources can be noisy or missed at low
sample counts. This estimator has cosine sampling without light importance
sampling, MIS or denoising.

## Lighting and visibility

Each Gaussian receiver uses the existing covariance normal estimate, oriented
toward the camera. Near-isotropic particles retain the existing view-facing
fallback. Cosine-weighted hemisphere rays estimate authored material emission;
Lambert's cosine/PDF factors cancel. The mean emission is added to the existing
ambient/direct irradiance, scaled by captured-light power, then multiplied by
the decoded captured color. Explicit scene lights keep their separate estimator
and are not added a second time.

The nearest authored geometry blocks an emitting surface behind it. The existing
shadow-strength control blends this visibility. With **Scan shadows** enabled,
other captured Gaussians attenuate the emitter segment using the existing
peak-opacity field. The receiver itself is excluded by stable source index,
including when visibility filtering/reordering changes native primitive indices.
This is not an extinction/albedo volume reconstruction. Distance haze attenuates
both the source-to-receiver segment and the existing receiver-to-camera segment;
shadow shafts and scattering integration remain unfinished.

Emission is constant over each primitive or imported material slot. Primitive
RGB is decoded before multiplying emission strength. Imported materials use their
linear `Ke`, matching authored-surface rendering. Hidden objects do not emit.
The emission estimator currently follows the first emitting surface, rather than
indirect paths through additional reflecting objects. **Surface bounces** still
controls the separate transport between authored surfaces.

Composition **v14** adds the strict Boolean `captureEmission`. Enabled recipes
include the complete v13 settings. Disabled recipes canonicalize to the existing
applicable schema, preserving prior descriptions and the default rendering path.
The setting participates in composition history, project storage and frozen
photo/film recipes. It remains a global scene setting during shot recall.

The native capture-shadow callback has its own payload and intersection table;
legacy solid-shadow payloads are unchanged. Software visibility implements the
same receiver exclusion. Scene/time replacements release retained particle
buffers. Temporal receivers and shadow geometry use the resolved capture time.
Existing GPU-batch cancellation, native textures and photo tiling remain in use.

## Independent renderer evidence

Native and software traversal each pass **17 rendered cases**, with maximum
channel error **0/255** against independent analytic or quadrature expectations:

- Lambert sphere solid angle, inverse-distance change and capture-power scaling.
- Receiver exclusion, source-index/primitive-index mismatch, single and
  overlapping captured occluders, solid/mesh blockers and shadow-strength blending.
- Emitter-segment plus observer-segment haze.
- Real OBJ/MTL material emission, rectangular box emission and hidden emission.
- Disabled default, preserved captured appearance and a moving temporal receiver.

Strict v14 validation, imported-asset save/reopen, exact native readback/tiling,
exact progressive final output and cancellation after an observed capture batch
also pass. Evidence:

- `/private/tmp/looksy-capture-emission-native-v5/receipt.json`
- `/private/tmp/looksy-capture-emission-software-v1/receipt.json`

These small independent tests establish the defined estimator behavior. They do
not establish dense-scene responsiveness or photographic reference parity.

All 17 retained PNGs also match byte-for-byte between native and software
traversal: `/private/tmp/looksy-capture-emission-native-software-v1.json`.

## Native workflow and dense scan

The new `gaussian_capture_emission_ui_test` exercises the real SceneWorkspace
checkbox with pointer input, undo/redo, captured/lit mode retention, sample and
effect edits, concurrent photo/sequence edits, project reopening, global shot
recall and compact-window pointer toggles. Its four-point fixture passes:
`/private/tmp/looksy-capture-emission-ui-v2/receipt.json`.

Two separate calibrated Bonsai workflows pass with **1,244,819 Gaussians**, using
the original 20-second per-action wait and 64 final lighting samples:

- Visible glowing sphere, scan shadows disabled:
  `/private/tmp/looksy-capture-emission-dense-ui-v1/receipt.json`.
- Off-camera glowing sphere, scan shadows enabled:
  `/private/tmp/looksy-capture-emission-dense-shadow-ui-v1/receipt.json`.

In the second workflow, enabling reception changes **48,527 pixels** by more
than one channel code in the 512×341 photo, with maximum channel difference
**40/255**. The emitting geometry is unchanged between these exports and outside
the frame; this isolates received captured-color lighting. It is an effect
comparison, not a photographic accuracy metric. The retained on/off photographs
and compact control captures were inspected.

Both workflows freeze a still while capture power changes and a twelve-frame
PNG sequence while emission reception is disabled in the editable scene. The
accepted v14 recipes survive export, reopen and saved-shot recall. The first
sequence independently replays all twelve frames exactly:
`/private/tmp/looksy-capture-emission-dense-replay-v1/receipt.json`.

The final compact-view observations are 2048×1364 at 64 samples: **238.95 ms**
without scan shadows and **956.19 ms** with them. These are different emitter
fixtures, not a controlled overhead comparison or sustained foreground latency
benchmark. Maximum observed lighting batch GPU times are 0.94/4.69 ms. Capture
illumination still completes before the current solid-lighting preview prefixes;
more complex source/mesh configurations can delay first useful feedback.

## Integrated app and regression scope

The desktop bundle builds. The focused Gaussian suite finishes with **41 passed,
0 failed and 1 skipped** out of 42 checks. The existing progressive-lighting
foreground test returns its unavailable-activation skip; it does not qualify
sustained input-to-photon latency. Log:
`/private/tmp/looksy-capture-emission-ctest-v1.log`.

The frozen packaged Main app loads the dense v14 scan-shadow/emission recipe,
renders **2700×1799 at 64 samples** in **2418.34 ms**, and completes the unchanged
30-second Scene render/control check in **11,321 ms**, with an active/exposed
window. Its object controls and rendered scan were inspected:
`/private/tmp/looksy-capture-emission-main-v1/receipt.json`, `scene-objects.png`.
This is a packaged Scene render/control result, not qualification of the whole
app, capture pipeline, sustained interaction or full reference fidelity.

The immutable bundle is
`/private/tmp/looksy-capture-emission-app-final-v1/Looksy.app`, executable SHA-256
`1f9f82862c51452bd1fe7300e25cdd1deb2f5f42aba7320b6e8ae1e43a562359`.
Its identity is checked before and after Main rendering; the dirty workspace's
later builds are outside that packaged proof.

The accepted scan-shadow recipe also exports a **2700×1799, twelve-frame**
lossless master at 64 lighting samples:
`/private/tmp/looksy-capture-emission-sequence-2700-v1/camera-path.json`.
Its first full-resolution frame was visually inspected. Both this master and
the 256-edge shadowed UI sequence replay all twelve frames exactly. Five retained
masters covering progressive lighting, repeated surfaces, spatial diffuse,
spatial reflections and legacy primary emitters also replay all sixty frames
exactly. These seven final checks total **84/84 exact RGB frames**, maximum
channel error 0:
`/private/tmp/looksy-capture-emission-final-replays-v1/runs.json` and each named
subfolder's `receipt.json`. The separately checked no-scan-shadow sequence adds
twelve exact frames; it is not included in that 84-frame total.

Final scoped source, executable, report and evidence hashes:
`/private/tmp/looksy-capture-emission-source-binary-final-v1.json`. The scoped
implementation/test identities remain unchanged throughout final qualification.
Capture completeness and sharpness, matched photographic lighting integration,
small-source convergence, dense combined-method responsiveness and sustained
foreground interaction remain open. **Full reference-video parity remains false.**
