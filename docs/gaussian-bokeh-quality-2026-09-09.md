# Extended bokeh sampling and interruption proof

The Aperture quality selector now offers 256, 512, 1024, 2048 and 4096 samples
in addition to the existing 16/32/64/128 levels. The default remains 32. These
are optional, more expensive settled-photo levels, not a new realtime claim.
The existing aperture sequence, shader and old sample prefixes are unchanged.

## Quality measurement

An isolated white Gaussian target behind a circular aperture has a nearly flat
defocused interior when aperture integration converges. The native GPU fixture
measures residual variation in that interior in linear light, away from the
blurred boundary, with grain/halation disabled. Exposure is increased only to
make quantized output suitable for measuring the dim integrated highlight.

Receipt: `/private/tmp/framehouse-bokeh-quality-20260909-v2/receipt.json`.

| Aperture samples | Interior coefficient of variation |
| --- | --- |
| 128 | 0.830111 |
| 256 | 0.432049 |
| 512 | 0.284405 |
| 1024 | 0.145144 |
| 2048 | 0.084110 |
| 4096 | 0.043337 |

The initial 1024 ceiling failed the declared smoothness gate (coefficient below
0.08 and at most 35% of the 128-sample baseline). The ceiling was raised rather
than loosening this gate. At 4096, variation falls approximately 94.8% relative
to 128; total integrated brightness differs by approximately 0.012%. Native
128/4096 images were inspected: the distinct sampling pattern becomes a smooth
disk with a soft boundary. No denoiser, sharpening or grain hides the error.

This is a bounded circular-pupil fixture, not proof that 4096 samples converge
every highlight, aperture shape, depth, output size or occlusion configuration.
Larger blur footprints may need more efficient/adaptive integration. The maximum
setting is 128 times the default aperture sample count, and large exports can
take minutes or longer. No full-size 4096-sample SUV final/export was timed.

## State and resource contract

- Existing lens descriptions retain `framehouse-thin-lens-v1`. Extended levels
  use `framehouse-thin-lens-v2`, so old readers fail explicitly rather than
  silently downgrading quality. Invalid sample counts and schema/count mismatches
  are rejected. Photo/film edits in QML preserve the appropriate lens version.
- Saved-camera serialization covers every level; actual disk save/reopen proves
  a 4096-sample shot retains its exact camera/photo state. Camera-path sampling
  preserves matching 4096-sample settings. Old paths/film regressions still pass.
- Aperture samples remain deterministic. For circular and rounded polygon
  apertures the old 128 samples are an exact prefix of the 4096 sequence.
- The existing bounded latest-only progressive queue publishes prefixes
  4/8/16/32/64/128/256/512/1024/2048 before the final 4096 image. The 2048 prefix
  matches an independent 2048 render exactly and remains immutable; the final
  GPU texture matches explicit readback pixels exactly.
- Film-only edits reuse the completed pre-film source and do not rerasterize
  4096 views. Grain/halation replay remains deterministic. Cancellation was
  observed after nine samples without publishing a canceled final image.
- The extra sample vector is small (4096 two-component offsets). Integration
  reuses the existing accumulation textures; it does not allocate thousands of
  full-frame images. This is a structural bound, not maximum-scene RSS proof.

## Actual viewer proof

Receipt: `/private/tmp/framehouse-bokeh-quality-ui-20260909-v1/receipt.json`.
`eligible: true`, `passed: true`, native Main.qml on the baseline one-million
SUV scene, capture camera 51. The test activates the actual quality control,
checks the new explanatory hint, observes a 4/4096 progressive frame, exercises
a film edit without losing the extended lens schema, then interrupts with
navigation and restores 16 samples. The screenshot was visually inspected.

The first observed prefix was recorded at 2426 ms (including screenshot work);
the navigation draft appeared 187 ms after interruption. Existing focus picking,
Escape/right-click cancellation, twelve cached film edits and normal optical
refinement also pass. The run shared the host with training and native tests;
these are individual contended timings, not clean input-to-photon benchmarks.

Six focused tests pass in 28.25 seconds: rendering/bokeh, viewport handoff,
temporal rendering, saved views, camera paths and film export. Build output
retains the pre-existing QString range-copy warning in the viewport handoff test.
`git diff --check` passes. Only native test targets were built; the installed app,
user originals, baseline scene and active high-detail training were not replaced.

## Still open

This improves optical sampling artifacts, not reconstructed geometry or detail.
Physical camera UI/persistence and measured scene scale, camera-relative precision,
calibrated RAW/radiance, lens-specific optics and measured grain/halation/film
response remain required. The separate high-detail reconstruction is still live;
no candidate quality improvement is claimed until its held-out images exist.
