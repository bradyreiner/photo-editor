# Dense shadowed haze performance

The 830,842-splat Bonsai case improves from **46,801.51 ms to 574.40 ms** at
640×427 with 16 light samples and scan shadows: **81.48× faster**. The actual
dense QML workflow also passes its unchanged 20-second waits, including a frozen
12-frame PNG sequence, reopening and compact controls. Full reference-video
quality and sustained foreground interaction remain separate qualifications.

## Frozen experiment

Evidence lives in `/private/tmp/looksy-haze-perf-v1/`. `plan-before.json` pins the
model, camera files, baseline executable and original benchmark driver before
editing. It limits the loop to three attempts and requires at least 80% lower
render time, ≥40 dB image agreement, ≤1 mean / ≤4 p99 / ≤20 maximum RGB channel
code error, ≤128 MiB RSS growth and ≤128 MiB logical field storage.

The native benchmark path is unchanged: the same validated model, camera,
640-pixel width, haze settings, light, blocker and sample count. Driver additions
provide optional native-texture/Retina comparisons, odd-origin tiles and cache
checks; the default dense scenario retains its original inputs. Host memory is
recorded by `/usr/bin/time -l`. The reported renderer time includes scene setup
and rendering, after the common numeric fixtures initialize the Metal pipelines.

| Measurement | Frozen baseline | Final candidate |
| --- | ---: | ---: |
| Render time | 46,801.51 ms | 574.40 ms |
| GPU time | 46,146.38 ms | 218.45 ms |
| Largest GPU lighting submission | 28.96 ms | 2.42 ms |
| Peak RSS | 757,170,176 bytes | 755,335,168 bytes |
| Peak process footprint | 1,496,171,624 bytes | 1,518,863,464 bytes |
| Combined field storage | Per-point integration | 27,263,952 bytes |

Against the original image, the final candidate measures **52.19 dB**, **0.99855
SSIM**, **0.366 mean**, **2 p99** and **3 maximum** channel-code error. The
candidate images were inspected. All frozen numeric/image/memory gates pass.
This comparison measures agreement with the earlier haze renderer, not with a
reference photograph or Octane.

## Three attempts

1. **Share primary-ray integration.** A depth-dependent field replaces repeated
   full integrals at every Gaussian center and solid/background pixel. This
   reaches 3,052.90 ms; a confirmation records 3,055.05 ms. The initial dense
   Retina UI still fails its 20-second haze-toggle wait.
2. **Bound viewport work by angle.** Native previews use a grid spacing based on
   focal length, while exports retain the fine four-pixel grid. Separate cache
   keys prevent a viewport result from satisfying a finer export. At 2700×1799,
   the native frame measures 4,507.52 ms versus 18,222.58 ms for the fine export.
   Their maximum RGB difference is 2 codes, mean 0.033, p99 1 and PSNR 62.94 dB.
   The dense UI now completes controls and photos, but exports only six of twelve
   frozen frames before its 20-second wait expires. That failure is retained.
3. **Parallelize identical strata.** Each visibility stratum runs independently
   on the GPU, followed by an ordered prefix pass. Sample count, locations,
   light seeds and export spacing are preserved. This produces the final
   574.40 ms result. The 640-pixel image is identical to the sequential shared
   field. The dense UI's full workflow passes; all twelve exported frames also
   replay with **zero RGB error** using the frozen sequential renderer.

The first two failures are not removed or replaced with longer waits.
`attempt2-plan-before.json` and `attempt3-plan-before.json` retain each follow-up
hypothesis. The loop stops after the third attempt.

## Model and resource behavior

The field stores accumulated explicit-light scattering against extinction mass
`u = 1 - exp(-density * distance)`. Base fog and transmittance remain analytic.
Primary splat fragments sample at their center depth, solids at their true hit
distance, and backgrounds at the configured distance. Secondary spatial rays
retain direct integration. Global grid coordinates, seeds and halo nodes keep
export tiles consistent. Each aperture sample gets its own field.

Visibility samples use temporary float32 storage; the ordered prefix uses
float16 storage. The **combined** logical allocation is capped at 128 MiB.
Automatic tiling includes post-effect halos and actual whole-pupil lens bounds
before allocating output. Exports retain their finer grid regardless of a
previously cached native preview. This remains approximate homogeneous single
scattering with creative base fill, not a multiple-scattering volume renderer.

## Verification scope

`dense-ui-attempt3/receipt.json` passes native checkbox input, undo/redo, quality
and effect retention, controlled photo export, a frozen 12-frame sequence during
a concurrent edit, reopening, shot recall and compact pointer controls. Its
captures were inspected. The last recorded dense viewport frame is 435.24 ms.
These checks do not establish sustained foreground input-to-photon latency.

`sequential-film-replay/receipt.json` verifies all twelve PNGs and their hashes
against the frozen sequential renderer, with maximum RGB error zero. The
2700-pixel viewport/export agreement is a single controlled pose. Broader
lighting, thin-volume detail, demanding optics and sustained interaction still
need representative qualification.

The numeric suite covers extinction, box/mesh and Gaussian shadow integrals,
finite lights, directional phase, multiple-light energy and reflected segments.
It also checks ordinary and odd-origin tiles, optical tile agreement,
native/export cache separation, cancellation, persistence, and 8K maximum-quality
allocation/cancellation. The 8K check does not claim a completed 8K beauty render.

The experiment changes are local and uncommitted. Frozen source, binary and
evidence identities are retained alongside the reports.

## Final confirmation

The rebuilt candidate confirms **582.54 ms** on the same dense benchmark; the
earlier 574.40 ms result and this confirmation both exceed the frozen 80%
reduction gate. `final-parallel-ctest.log` passes **18/18 tests in 80.78 s**.
All **18** native/software numeric-fixture PNG pairs are byte-identical, maximum
center-channel oracle error is 0.805, ordinary and odd-origin tiles are exact,
and the tested optical tiles are exact. Native/export cache separation and
same-domain reuse pass. Both 8K maximum-quality allocation/cancellation cases
pass without publishing partial output.

The rebuilt packaged Looksy app renders the dense project with the Haze control
visible: `packaged-main/receipt.json`, 8.86 s. Its window is exposed but inactive,
so this is not foreground-latency qualification. A neutral full-resolution
held-out camera has zero RGB error against the frozen source-quality evaluation
render; the original unshadowed dense PNG is also unchanged.

`final-identities-before.json` and `final-identities-after.json` verify fourteen
source files, six current binaries, frozen model/camera inputs and the baseline
executable. `final-image-audit.json` and `final-proof-runs.json` retain the final
checks. The three-attempt loop is accepted; its remaining qualifications are not
reported as complete.
