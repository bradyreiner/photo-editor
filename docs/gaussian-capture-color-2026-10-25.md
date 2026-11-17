# Capture color preservation — October 25, 2026

Capture preparation previously sent already-rendered JPEG, PNG, TIFF and decoded
video frames through the editor's default photographic develop. That applied an
additional display rendering and darkened the training images. A real Bonsai
JPEG versus its prepared image scored only 19.0698 dB / 0.636788 SSIM, with a
maximum channel difference of 78 codes. The synthetic sRGB PNG regression
reproduced a maximum difference of 79 codes before the fix.

Processed images now use source ICC-to-sRGB conversion, sharing the existing
source-content thumbnail policy. Camera-linear RAW sources retain their neutral
develop path. The global editor renderer, RAW decoding and existing trained
artifacts are unchanged. Prepared datasets record
`capture-source-color-to-srgb-v2` and each frame's interpretation. Timed capture
validation accepts both the earlier contract and v2.

## Regression and controlled reconstruction

`gaussian_capture_color_test` covers sRGB and Display P3 PNG/JPEG/TIFF inputs.
After the change, five cases have zero channel error against source ICC
conversion; P3 TIFF has maximum 1 code and mean absolute error 0.02357. Limits
were fixed at maximum 3 and mean 0.35. A real DSCF5566 JPEG and its new prepared
frame are pixel-identical. Timed-capture coverage includes legacy compatibility.

The accepted 96-photo alignment remains fixed for a controlled color-only
reconstruction. The original 5000-step model is preserved. On 37 independently
held-out photographs it scores 17.39477 dB mean PSNR / 0.413328 mean SSIM, with
14.67094 dB worst-view PSNR. It fails all predeclared targets (28 dB, 0.9 and
22 dB respectively). Native versus Brush rendering at one identical camera is
75.92257 dB, locating the large darkness error upstream of native rendering.

The corrected-color preparation uses the same 96 originals. COLMAP refreshes
only seed point colors, changing 45,247 colors while retaining point geometry,
tracks and all poses exactly. The trainer receives the corrected dataset with
the same 5000 steps, 1600-edge limit, 500,000-splat cap, seed 42, refinement
interval 96, growth stop 4000 and every-eighth internal evaluation split.
All twelve internal held-out names match the original experiment. The 37
external reference photographs and their evaluation-only camera poses remain
outside training. No reference pixels or poses are fitted.

Training completes successfully in 318.20 seconds. The trainer reports internal
held-out PSNR 25.293802 and SSIM 0.8629868; these are not the external 37-view
metrics. The external comparison is recorded separately under
`/private/tmp/looksy-capture-color-correction-v1/heldout-5000-v1/`, with
before/after source, model and binary identities. Its quality targets remain
unchanged. Full reference-video parity is still open.

The 37-view external comparison completes in 236.66 seconds with every pinned
identity unchanged. Color correction improves mean PSNR **17.39477 → 22.02049
dB**, mean SSIM **0.413328 → 0.649668**, and worst-view PSNR **14.67094 →
18.95436 dB**. All three absolute targets still fail. Native versus Brush at an
identical corrected-model camera scores 74.16022 dB, with maximum channel error
2; the substantial reconstruction error is not a comparably large native
display mismatch. The corrected model reaches its 500,000-splat cap.

Full-resolution DSCF5565/DSCF5637 crops were inspected. The correction restores
shadow detail, but the pot, trunk, bicycle and blanket still lose fine structure.
Residual source-camera differences and insufficient training capacity are both
possible contributors; the color experiment does not isolate either one.

## Next bounded capacity experiment

`quality-15000-1m-v1/plan-before.json` fixes the corrected dataset and camera
mapping, trainer identity, seed, resolution, refinement interval and held-out
names. It changes the training budget jointly to 15,000 steps, one million
splats and growth stop 12,000 (3,000 settling steps). Separate 5,000-step
checkpoints preserve progress. This is a budget experiment, not an isolated
step-count or splat-count claim. The same external targets and unmodified
reference photographs apply; neither evaluation camera poses nor reference
photos are supplied to training. The receipt and owned process determine the
run's state. Training completes successfully in 1,221.68 seconds, retaining all
dataset and trainer identities. The final artifact has exactly one million
splats, SHA256
`1aa609afacb67b0cf1629a212d83d4e1e81ad5b49adf42a25c8fd98d473508f6`.
Its internal evaluation reaches 26.295061 dB / 0.88852787 SSIM. At one identical
camera, native versus Brush is 73.25345 dB with maximum channel error 2. These
internal checks do not establish the external quality targets; the separately
pinned 37-view evaluation determines that result.

The larger candidate's external run completes in 348.19 seconds with all model,
reference, camera and binary identities unchanged. It does **not** provide a
meaningful overall quality improvement:

| External 37-view metric | Corrected 5000 / 500k | Corrected 15000 / 1m | Target |
| --- | ---: | ---: | ---: |
| Mean PSNR | 22.02049 dB | 22.07054 dB | ≥28 dB |
| Mean SSIM | 0.649668 | 0.645735 | ≥0.9 |
| Worst-view PSNR | 18.95436 dB | 18.54281 dB | ≥22 dB |

Only 19/37 PSNR and 6/37 SSIM values improve. The same worst-PSNR photograph is
DSCF5565; full-resolution images and its worst crop still show softened and
smeared structures. `quality-15000-1m-v1/comparison.json` retains every named
delta and all three failed absolute gates. The larger schedule is not promoted
to a product preset. Internal training scores improve by about 1 dB while the
external result stays nearly flat, so further step/splat increases alone are
not justified by this experiment.

Ten focused regression checks pass, including capture color, timed capture,
alignment/mapping diagnostics, cleanup/controller behavior and native photo/video
options. The packaged Main Scene render/control proof also passes in 11.58
seconds, exposed but inactive; it does not prove foreground input latency or a
fresh capture-to-export lifecycle. Evidence:
`/private/tmp/looksy-color-lasso-final-ctest-v1.log` and
`/private/tmp/looksy-color-lasso-packaged-main-v1/receipt.json`.

After adding camera-depth cleanup, the final focused set passes 11/11 and the
rebuilt packaged Main Scene render/control proof passes in 12.025 seconds,
exposed but inactive. Final evidence:
`/private/tmp/looksy-capture-depth-final-ctest-v1.log` and
`/private/tmp/looksy-capture-depth-packaged-main-v1/receipt.json`. This remains a
render/control qualification rather than foreground latency or a fresh
capture-to-export lifecycle.

Evidence roots:

- `/private/tmp/looksy-capture-color-before-test-v1.log`
- `/private/tmp/looksy-capture-color-after-test-v1.log`
- `/private/tmp/looksy-bonsai-source-capture-v1/heldout-5000-v1/`
- `/private/tmp/looksy-capture-color-correction-v1/plan-before.json`
- `/private/tmp/looksy-capture-color-correction-v1/training-run/receipt.json`

This experiment deliberately reuses the accepted source-derived geometry to
isolate color preparation. It is not a fresh corrected-color alignment run or
an integrated Create-to-finished-film qualification.

A separate fresh production alignment is now recorded under
`/private/tmp/looksy-corrected-color-alignment-v1/`. Its active before-run plan is
`plan-before-v2.json`: the same 96 originals and 37 excluded reference names,
same shared-camera/strict-matching options and pinned COLMAP binary, new v2
preparation and sparse reconstruction. Coverage must remain 96/96; absolute
camera gates remain fixed, with additional improvement diagnostics recorded
before running. The first attempt rejected before creating a project because
the standalone driver could not discover Brush; its logs and first plan are
retained. V2 explicitly configures the same pinned binaries. No new camera or
photographic gain is claimed before that run and its audit finish.

`/private/tmp/looksy-capture-color-depth-source-binary-v1.json` pins the final
Gaussian sources, app/test binaries, completed comparison models and evidence
for this dirty-tree snapshot. The ongoing alignment receipt is intentionally
excluded from immutable evidence hashes; its frozen plan is included.
