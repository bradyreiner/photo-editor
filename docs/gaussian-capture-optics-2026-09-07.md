# Gaussian capture optical metadata

Canonical preparation previously converted photographs to sRGB PNGs without
carrying camera metadata into reconstruction. The 32-photo reference capture
started with heuristic focal lengths despite usable EXIF. Explicit priors now
preserve this information without changing prepared pixels.

## Contract

ImageIO reads photographic optics on macOS; LibRaw reads RAW metadata without
unpacking pixels. Records include make/model, lens, body serial and physical
and equivalent focal lengths, not GPS or arbitrary maker-note payloads.

Valid 35mm-equivalent focal length becomes a SIMPLE_RADIAL starting estimate:
`f_pixels = focal35_mm / 43.27 * hypot(width, height)`, following
[COLMAP 3.13's diagonal convention](https://raw.githubusercontent.com/colmap/colmap/3.13.0/src/colmap/sensor/bitmap.cc).
It is not calibrated ground truth. Missing/invalid values retain heuristic
estimation; physical focal length alone does not invent sensor size.

Generated image-list batches preserve priors after PNG preparation using
[COLMAP camera reader options](https://raw.githubusercontent.com/colmap/colmap/3.13.0/src/colmap/controllers/image_reader.cc).
Shared intrinsics require matching make/model, body serial, lens, physical focal
length, equivalent focal length, source format and prepared dimensions. Incomplete
identities get per-frame cameras. RAW and JPEG remain separate because distortion
and crop processing can differ. These rules are not full lens calibration.
The worker writes atomic lists; the controller validates project-local paths and
sequences extraction before matching. Receipts and UI disclose fallback counts.
Non-macOS builds currently expose LibRaw optics only; JPEG support remains open.

## Evidence

Final real run: existing 32 Panasonic DMC-TZ3 JPEGs, COLMAP 3.13, Brush, 800-pixel
edges, 3,000 training steps. All 32 views register with 32 EXIF priors and no
heuristic-only frames. Completed database inspection confirms 32 cameras with
prior-focal flags and 32 distinct image camera IDs. EXIF 28mm equivalence yields
647.099607 pixels at 800x600, replacing the previous 960-pixel heuristic. Missing
body/lens identity prevents sharing.

The validated scene contains 19,115 splats; elapsed time is 86.922 seconds. A
50ms event-loop heartbeat records a maximum 64ms gap, not foreground framerate.
Held-out training-view PSNR is 21.366087 dB, SSIM 0.5870927. Camera reconstruction
uses all views; poses are not independently held out. Inspected output still has
blur, ghosting and poor foliage. An earlier make/model-only sharing attempt
scored 20.14837 dB / 0.57127726 and was superseded. The historical no-prior run
scored 21.056559 dB / 0.5849426. Single stochastic runs and differing undistorted
geometry do not establish a controlled quality gain.

All 32 final prepared PNGs are byte-identical to the existing no-prior prepared
directory (`diff -qr` passes). Originals were not modified. A real Nexus 6 DNG
prepares through the canonical color path with LibRaw optics, correctly reporting
zero priors / one estimated frame because equivalent focal metadata is absent.
This is not multiview RAW reconstruction.

Eight focused Gaussian tests pass with native Metal. Two sandbox-only GPU tests
failed because Metal was unavailable; native rerun passed all. Real DNG/JPEG mask
checks pass four working spaces, cropped/uncropped cases, seven raster modes,
protected pixels and asynchronous publication. Desktop build and QML lint pass.
An isolated packaged-app run reopens the scene, produces a native GPU frame,
visibly displays the 32/0 prior label and shuts down its empty-store runtime.
Screenshot inspected; its catalog recovery banner is intentional isolation, not
the user's database. No model weights installed or loaded.

Evidence paths (temporary, not durable fixtures):

```text
/private/tmp/framehouse-gaussian-optics-w6e9pp/identity-safe-receipt.json
/private/tmp/framehouse-gaussian-optics-w6e9pp/identity-safe-project/scene.json
/private/tmp/framehouse-gaussian-optics-w6e9pp/raw-preparation.json
/private/tmp/gaussian-optics-raw-color-regression.log
/private/tmp/gaussian-optics-native-tests.log
/private/tmp/gaussian-optics-final-build.log
/private/tmp/gaussian-optics-qmllint.log
/private/tmp/gaussian-optics-ui-6BgLAA/receipt.json
/private/tmp/gaussian-optics-ui-6BgLAA/startup.png
```

Reconstruction fidelity, calibrated multiview RAW capture, camera/distortion model
selection, mixed orientations/crops, backend packaging, foreground realtime,
temporal 4D and physical film emulation remain unresolved. Per-camera process
startup cost needs larger-capture profiling. The broader active goal remains open.
