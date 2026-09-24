# Stacked mask sampling — 2026-09-07

## Corrected behavior

The preceding mask-thumbnail fix prepared the global pre-layer look correctly,
but every mask still sampled that same image. The renderer instead evaluates a
later range mask on pixels changed by earlier local adjustments. Its edge-aware
refinement has a different contract: it retains the immutable pre-layer image.

The worker now follows canonical recipe order, advances earlier local adjustments
once, and samples each requested mask with both the current range pixels and the
original edge reference. Partial refreshes and reversed UI row order do not
change the result. The canonical compiler controls enabled, bypass and solo
behavior; disabled sets can remain compiled with zero effect.

Two reusable engine operations in `local_mask_sampling.h` expose that distinction:

- `apply_local_adjustments_to_look_pixels_with_context` continues the shared
  local-adjustment kernel while retaining a caller-owned original edge image.
  It rejects aliased or mismatched reference surfaces. Ordinary rendering still
  uses its existing entry points and owns its edge reference as before.
- `rasterize_local_mask_set_alpha_with_context` uses current pixels for range
  selection and the original pixels for edge refinement. Both surfaces must
  match the declared dimensions; incomplete surfaces fail closed. Existing
  rasterizer overloads retain their original single-source behavior.

Group-level inversion, expansion, smoothing, fine-edge/edge-contrast refinement,
opacity and density now also participate in composite thumbnails. The shared
`compile_local_mask_set_settings` helper supplies both the renderer compiler and
the thumbnail worker, avoiding a separate interpretation of these settings.
Individual source thumbnails continue to show standalone source coverage rather
than inheriting the group's post-composition refinement.

The worker sorts requested rows internally but publishes keyed entries, preserving
UI identity. Each preceding adjustment runs once; no full prefix replay or
per-set photographic-surface cache is required. This is a structural work reduction,
not a foreground latency benchmark. Final-alpha cache keys use `mask-input-v3`.

## Verified evidence

- An independent numerical fixture recovers mask coverage from the difference
  between actual stacked-render pixels and full-coverage adjustment pixels.
  Recovered alpha and the new context-aware rasterizer agree exactly in this
  fixture (maximum byte delta 0). Substituting the changed pixels for the original
  edge reference changes 32 samples. Continuing the stack with the new engine
  operation also reproduces all RGBA floating-point output values exactly.
- `mask_thumbnail_color_test` exercises two synthetic ICC-tagged JPEGs and the
  read-only real Nexus 6 DNG/JPEG pair in four built-in working RGB spaces,
  with/without crop. The DNG explicitly requires camera-linear RAW decoding.
- Each image/profile/crop combination exercises two preceding edits, a disabled
  preceding mask, preview bypass, solo selection and reordered recipe masks.
  It also compares a partial target-only refresh with a reversed three-row
  refresh. Source and group thumbnail alpha must match the canonical engine
  exactly, including group refinement and an edit on the target itself.
  The real pair covers 80 stacked-mask combinations plus 16 reversed-row checks;
  synthetic fixtures cover the same matrix.
- For the real uncropped ProPhoto DNG, ignoring the preceding edits changes 8,395
  samples across the target's source/group thumbnails; the cropped case changes
  10,626. These counts isolate the wrong range input, not independent subjective
  mask quality. Final/old-input RAW group PNGs were visually inspected.
- Five focused tests pass: engine color pipeline, render executor, editor
  layer-adjustment controller, source thumbnails and mask-thumbnail color.
  The application builds successfully; scoped whitespace checks pass.

Receipts:

```text
/private/tmp/mask-stack-accepted-build.log
/private/tmp/mask-stack-final-build.log
/private/tmp/mask-stack-accepted-tests.log
/private/tmp/mask-stack-raw.log
/private/tmp/mask-stack-jpeg.log
/private/tmp/mask-stack-raw-stack.png
/private/tmp/mask-stack-raw-stack-old-input.png
```

## Remaining work

Settled live outlines and selection/range raster previews are addressed in
`live-mask-sampling-consistency-2026-09-07.md`; that follow-up retains explicit
foreground/full-resolution and other overlay limits. Selection-scoped
adjustment-thumbnail alpha still has a separate source-sampling path.
Full-resolution edge accuracy, cross-resolution
RAW highlights, custom ICC/DCP assets, larger camera/capture corpora and foreground
interactive latency are not proved by thumbnail tests. No Photoshop-completeness,
realtime, independent color calibration, embedded-model-weight installation or
3D/4D reconstruction promotion follows from these results.

Source inspection also found an existing single-brush fast-path eligibility
contradiction: `brush_mask_source_has_default_refinement` requires an empty stored
matte, while its caller requires a nonempty stored matte. It therefore appears
unreachable. Before enabling it, its exposure blending must be compared with the
canonical compiled adjustment, because its formula differs. This pass leaves
that optimization untouched; it is not evidence of a current visible color bug.
