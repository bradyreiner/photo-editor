# Full-resolution Gaussian reconstruction diagnostics

This checkpoint enables native-pixel evaluation of photographic exports. It
does not improve reconstructed geometry, claim sharper photos, or certify the
optics/film model. The photographic-quality goal remains active.

## Change

The independent stored-RGB PSNR/SSIM tool previously rejected images above
16 megapixels and allocated two full-image arrays of five double moments per
pixel. That design prevented evaluating the existing 24 MP and larger export
path without resizing or very large working buffers.

`gaussian_image_quality_test` now uses an eleven-row circular source buffer and
one filtered row. Vertical-then-horizontal summation, 11x11 sigma-1.5 weights,
zero padding, RGB channel order and PSNR accumulation order are preserved. The
old dense SSIM implementation remains a small-fixture oracle, not the evaluation
path. There are no evaluation tile seams, downsampling or identity shortcuts.

Supported dimensions are now 1–8192 pixels on each axis, matching the explicit
still-export edge limit. This deliberately rejects extremely thin images wider
or taller than 8192, even if their total pixel count formerly fit below 16 MP.
File dimensions are checked before decoding. The tool does not auto-orient,
register, resize or color-transform inputs; matching camera/pixel domains remain
the caller's responsibility. The metric is still stored 8-bit RGB, not scene
radiance, perceptual calibration or an MTF/edge-acuity measurement.

At 6000x4000 the moment scratch changes from a theoretical 1,920,000,000 bytes
under the old algorithm to 2,880,000 bytes. At width 8192 it is 3,932,160 bytes.
These are algorithmic buffer sizes, not total process memory: decoded source and
candidate images, format conversions and codec allocations are additional.

## Verified evidence

- All 132 small dimension combinations match the dense oracle exactly, covering
  truncated halos and repeated circular-buffer wraparound. Every source row is
  loaded once in ascending order. Existing identity, known error, symmetry,
  invalid image, published float SSIM reference and report tests also pass.
- Oversize PNG and missing-file input rejection are covered, including direct
  image comparison rejection and file-reader admission.
- Registered CTest `gaussian_image_quality_24mp_test` evaluates a real 6000x4000
  allocation with three nonoverlapping white impulses on black, exercising both
  image corners and an interior position. Independent dense 32x32 response
  patches predict its full-image score. MSE is exactly 1.25e-7; measured SSIM
  is 0.9999956770535149 versus predicted 0.9999956770535154.
- Both focused CTests pass in 74.85 seconds (24 MP fixture 73.76 seconds), under
  concurrent reconstruction/evaluation. This is CPU diagnostic runtime, not a
  viewer or GPU performance claim.
- The maximum 8192x8192 (67,108,864-pixel) fixture also passes: MSE is exactly
  4.470348358154297e-8; SSIM is 0.9999984539938628 versus independently predicted
  0.9999984539938624. `/usr/bin/time -l` records 195.85 seconds wall time,
  556,220,416 bytes maximum resident set size (approximately 530 MiB), and zero
  swaps. This used two full RGB32 images and 3,932,160 moment-scratch bytes;
  it is not a codec-inclusive maximum-size PNG/JPEG or GPU export memory proof.
  The fixture ran at lower CPU priority alongside existing training and other
  diagnostics; its elapsed time is not an uncontended performance baseline.
- An actual source-copy JPEG, `source/IMG_0829.JPG`, decodes and compares at its
  full 5712x4284 dimensions: identity true, MSE/max channel error zero, SSIM 1.
  This exercises real large-image file decoding and the full metric path without
  an identity shortcut. A self-comparison is admission/correctness evidence only,
  not source-versus-reconstruction or RAW photographic fidelity.
- All eight real SUV source-versus-native frames were reevaluated. The entire
  report and all 24 crop PNG files are byte-identical to the dense baseline,
  checked with `diff -rq`. Mean PSNR remains 20.61026306402771 dB and mean SSIM
  remains 0.6042296436188497. The frame-8 first crop was visually inspected:
  vehicle-boundary deformation and translucent streaks remain plainly visible.

Evidence directory:
`/private/tmp/framehouse-full-resolution-metrics-akfLsi/baseline-recheck/`.
Original report/crops:
`/private/tmp/framehouse-reconstruction-quality-kINkJZ/native-versus-source/`.
The comparison reads the existing native images; it does not rerender the scene
or establish a new renderer parity result.

## Reproduction

```sh
cmake --build --preset build-dev-macos-debug --target gaussian_image_quality_test -j4
ctest --test-dir build/dev-macos-debug -V -R '^gaussian_image_quality(_24mp)?_test$'
build/dev-macos-debug/tests/gaussian_image_quality_test --large-image-test 8192 8192
```

`--compare REFERENCE CANDIDATE` still returns a single comparison;
`--evaluate REFERENCE_DIRECTORY CANDIDATE_DIRECTORY NEW_ABSOLUTE_OUTPUT_DIRECTORY`
still writes no-clobber named-frame reports and native-pixel triptychs. Large
synthetic fixtures identify themselves as metric proofs with `qualityCertified`
false. They are not evidence that any real reconstruction has improved.

## Next photographic gate

This checkpoint revalidated exact trainer PID 24355 and parent 16095 as live,
then implemented and tested the missing full-resolution evaluation capability. The existing
3200-edge/30,000-step/up-to-two-million-splat run was not restarted or modified.

When a candidate artifact is available, evaluate source/native/trainer agreement
and fixed subject/silhouette/texture regions at an explicitly matched camera and
resolution. Native-resolution source references must be genuinely decoded and
undistorted from original captures, not enlarged 1600/3200-pixel inputs. Changed
camera alignment must not be mislabeled as a controlled resolution-only gain.
Then use the observed failures to choose reconstruction/filtering changes before
enabling film effects. Calibrated RAW/radiance, measured optics/film, trustworthy
geometry and foreground realtime/progressive viewer proof remain separate gates.
