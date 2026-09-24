# Mask-thumbnail color consistency — 2026-09-07

## Fixed in this pass

The worker's mask thumbnails display grayscale coverage, not photographic RGB.
Their old sampling path nevertheless affected coverage: it discarded decoder
metadata, converted the source to 8-bit premultiplied RGB, used the default
working profile, approximated white balance/exposure, and omitted global
tone/curves/look edits. Luminance and color masks therefore sampled different
values from the engine's pre-layer input.

`thumbnail_worker_renderer_factory.cpp` now retains the source's decoder metadata,
linearizes through the same working-image bridge as previews, preserves device
RGB when RAW characterization is required, and invokes the engine's normalization,
camera characterization, scene-adjustment, tone, curves and look stages. Cropped
RAW inputs retain full-source highlight analysis at the decode resolution.
Mask rasterization receives the actual working-space configuration and floating
point pixels, without display tone mapping or an 8-bit photographic intermediary.

Mask evaluation also now precedes square thumbnail resizing. Source samples and
spatial mask coordinates share the photo's aspect ratio; the completed matte is
then resized/cropped for the square row preview. Grayscale and solid-color overlay
payloads share the same final alpha. A content-addressed `mask-input-v2` alpha-cache
key includes the native alpha hash, dimensions, source count and target edge.
The existing row cache is process-local (`ThumbnailCacheSession`); it does not
load earlier executable versions' mask entries from disk.

`mask_thumbnail_payload_from_alpha` reuses the existing display/metadata contract
for prepared alpha, and rejects invalid dimensions or incomplete byte payloads.
The old QImage-based evaluator remains available to its other callers; this pass
does not silently claim those paths have been corrected.

## Proof

- New `mask_thumbnail_color_test`: sRGB and Display P3 JPEG gradients, plus
  optional read-only photographic fixtures; four built-in working spaces
  (linear sRGB, Display P3, Rec.2020 and ProPhoto RGB), each with/without crop.
  It compares source luminance/color masks and their composite against public
  engine stages and rasterization, requiring exact final alpha bytes and
  RGB-equals-alpha PNG output. Cancellation and alpha-cache metadata are checked.
- Real 4208×3120 Nexus 6 DNG and matching JPEG both pass all eight profile/crop
  variants each. The DNG test explicitly requires camera-linear RAW decoder
  metadata. These are 96-edge thumbnail-input tests, **not** full-resolution
  mask tests. Compared with the old sampling route, the uncropped ProPhoto DNG
  case changes 21,088 alpha samples across three 96×96 thumbnails; the cropped
  case changes 22,412. These counts demonstrate regression sensitivity, not
  independently calibrated photographic quality.
- Existing controller/layer-thumbnail proof, source-thumbnail proof and new
  mask-color proof pass together. The combine oracle now performs its independent
  add/subtract/intersect/exclude/replace arithmetic before resizing, retaining
  its existing one-byte tolerance for intermediate alpha quantization.
  Combining separately resized mattes is not mathematically equivalent.
- The application builds successfully. The real RAW new/legacy mask PNGs were
  visually inspected; no foreground or interactive UI performance claim is made.

Receipts:

```text
/private/tmp/mask-color-accepted-build.log
/private/tmp/mask-color-accepted-tests.log
/private/tmp/mask-color-accepted-real.log
/private/tmp/mask-color-raw-final.log
/private/tmp/mask-color-jpeg-final.log
/private/tmp/mask-color-raw-final-mask.png
/private/tmp/mask-color-raw-final-legacy-mask.png
```

Earlier logs `mask-color-focused.log` and `mask-color-focused-final.log` contain
intermediate failures: missing alpha-cache metadata, then the old post-resize
combine oracle. They are not accepted final evidence.

## Still open

- These tests establish integration parity with the shared engine, not an
  independent color-science reference, camera corpus calibration or human mask
  quality evaluation.
- The new input is the global pre-layer look. Later mask sets can sample pixels
  changed by preceding local adjustments in the engine; that sequential-mask
  context is addressed for thumbnails in
  `stacked-mask-thumbnail-consistency-2026-09-07.md`; overlays still need follow-up.
- Legacy outline/range-preview evaluators and selection-scoped adjustment
  thumbnail alpha have separate source-sampling paths and need follow-up.
- Small thumbnail decoding/resampling cannot establish full-resolution hair,
  edge-aware or range-mask parity; cross-resolution highlight behavior, geometry
  transforms, custom ICC/DCP assets and additional camera families remain open.
- Loading/realtime latency, embedded model weights, genuine 4D reconstruction,
  complete professional editing-tool coverage and the broader active goal are
  not completed by this change.
