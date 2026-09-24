# RAW diagnostics and scope format correctness

This follow-up advances RAW/color and editing validation; it does not complete
the full app objective in `active-app-completion-goal.md`.

## Fixed and verified

The broad controller test's model/color assertion expected camera-linear
decoding to apply white balance. Live diagnostics reported unbalanced RGB and
deferred white balance, matching the shipping LibRaw unity multipliers and the
existing decoder ownership tests. The assertion now requires white balance
absent from applied controls, present in deferred controls, and an unbalanced
pixel contract. Failure output now includes the actual diagnostics.

The subsequent scope assertion forced its expected image to ARGB32, despite
the renderer producing a half-float scope surface before byte canvas encoding.
The test now computes independent 4096-to-256 bins and waveform averages from
that high-precision surface. It additionally checks every RGB sample against
the published canvas within one 8-bit level, verifies final-display domain and
matching dimensions, and retains the check against stale unedited input.
No production precision was reduced to satisfy the test.

A separate production bug was exposed in `histogram_scope_service.cpp`:
ARGB32-premultiplied images took the direct byte path without unpremultiplying
RGB. Identical straight colors therefore appeared darker solely because of
alpha storage. That path now calls `qUnpremultiply` before sampling; opaque and
high-precision paths retain their existing behavior.

The new regression fails before the fix (straight format 5 vs premultiplied
format 6) and passes afterward. It compares partial-alpha primaries across
straight/premultiplied pairs for ARGB32, RGBA64, RGBA16FPx4 and RGBA32FPx4:
RGB/luminance bins, waveform, density, vectorscope and clipping counts. Primaries
avoid explaining away a mismatch as lossy alpha round-trip quantization.

## Terminal receipts

- `/private/tmp/controller-diagnostics-run.log`: original RAW ownership mismatch.
- `/private/tmp/controller-wb-decoder-report.json`: synthetic 128x128 DNG,
  executable/runtime identity, explicit transform ownership, unchanged decoded
  pixels under WB override, bounded preview and four concurrent decodes pass.
- `/private/tmp/scope-alpha-before.log`: new alpha regression fails before fix.
- `/private/tmp/scope-alpha-fixed-build.log`: app and relevant test targets
  build successfully; packaged runtime still reports no installed model weights.
- `/private/tmp/scope-alpha-focused.log`: histogram service/coordinator/session
  and RAW decoder tests, 4/4 pass in 0.29 seconds.
- `/private/tmp/controller-scope-domain-final.log`: the existing
  `PHOTO_EDITOR_TEST_SCOPE_DOMAIN_ONLY=1` controller run passes. This covers the
  preceding diagnostics and final-display, working, scene-linear and proof
  domain checks, not the entire monolithic controller test.
- `/private/tmp/controller-scope-alpha-broad-final.log`: full run advances past
  the repaired checks, then stops at `the first slider edit should schedule a
  settled full-resolution render`. The current scheduler queues a pending full
  request behind an immediate draft for global-tone edits; that test's timer
  assumptions need separate runtime-contract verification. No full-suite pass
  is claimed. Scoped `git diff --check` passes.

## Next evidence gaps

The controller run also emits two invalid-primaries QColorSpace warnings for
ProPhoto-like EXR chromaticities. Source tracing reaches
`openexr_qcolor_space` in `photo_render_utils.cpp`, whose invalid-color-space
fallback labels unchanged pixels linear sRGB. This is a potential source-color
misidentification defect requiring an actual EXR pixel/metadata comparison,
including the float chromaticity boundary case and ACES/custom primaries;
do not treat the warning as harmless or claim it corrected here.
Subsequent correction, independent corpus proof and remaining EXR gaps are
recorded in `exr-source-color-2026-09-07.md`.

These tests are synthetic numerical/runtime evidence, not real-camera color
calibration or foreground scope/viewport interaction proof. HDR scope scaling,
nonfinite samples and full-resolution latency remain separately unverified.
Gaussian 3D/4D training, embedded model weights/search/agent quality, loading,
and the rest of the professional tool inventory remain in the full goal.
