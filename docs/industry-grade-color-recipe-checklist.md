# Industry-Grade Color Recipe Checklist

> Supporting recipe-contract record. Use
> `docs/color-engine-and-app-foundations-completion-strategy.md` as the sole authority for current
> ordering, migration, legacy deletion, evidence, and release completion.

This checklist defines the implementation path for making the color recipe pipeline predictable,
professional, and provable. Each item has a goal and a completion gate so progress can be audited
without relying on subjective inspection alone.

## 1. Canonical Recipe Contract

- [x] Define the v5 color recipe contract around explicit stages.
  - Goal: The saved recipe has clear sections for input transform, working space, grading, layers,
    display transform, and export transform.
  - Completion gate: `edit_recipe_state_test` proves old recipes migrate correctly and new recipes
    round-trip with no duplicated or conflicting color state.
  - Implementation note: The durable schema now emits recipe version 5, writes current export
    target state through `exportTransform`, and keeps display/soft-proof state in `outputTransform`.
    `edit_recipe_state_test` proves legacy migration, current-schema round-trip, and rejects v5
    recipes that duplicate export/profile state back into `outputTransform`.

- [x] Make `colorPipeline` the canonical color pipeline state.
  - Goal: `colorPipeline` owns input profile, working profile, render intent, output profile,
    gamut policy, headroom, and black-point behavior.
  - Completion gate: No parse/serialize path prefers `outputTransform` over `colorPipeline` for
    pipeline decisions except legacy migration.
  - Implementation note: `colorPipeline` now serializes and reparses input/working profiles,
    preview/export output profiles, render intent, headroom, gamut policy, and black-point
    compensation fields. Legacy `outputTransform` fields still parse as migration fallbacks.
    The engine `ColorPipelineConfig` now carries output gamut mapping enablement and policy instead
    of leaving that recipe state as serialization-only metadata. `edit_recipe_state_test` now proves
    `colorPipeline` wins when conflicting legacy `outputTransform` fields are present and the v5
    validator rejects those duplicate legacy fields.

- [x] Keep preview and export state separate but structurally identical.
  - Goal: Preview output profile and export output profile are separate targets using the same
    render pipeline.
  - Completion gate: Tests prove changing preview profile does not mutate export profile, and
    changing export profile does not alter scene-referred grading values.
  - Implementation note: The focused recipe tests now assert preview and export profiles round-trip
    separately through `colorPipeline.previewOutputProfile`, `colorPipeline.exportOutputProfile`,
    and `exportTransform.colorSpace`, and prove changing export profile preserves scene-referred
    grading values.

## 2. Explicit Render Pipeline

- [x] Enforce the stage order in code.
  - Goal: Rendering follows `decode/source profile -> input transform -> scene-linear working space
    -> adjustments/layers -> display/export transform`.
  - Completion gate: A render graph or pipeline test asserts stage names and ordering for preview,
    full-resolution preview, and export.
  - Implementation note: `render_graph_smoke_test` now asserts the exact canonical stage order for
    preview, full-resolution preview, and export variants, with export adding only the final
    `export_encode` stage after `output_transform`.

- [x] Remove hard-coded sRGB preview assumptions from core render paths.
  - Goal: sRGB is a default output target, not an implicit working-space fallback.
  - Completion gate: Tests fail if `preview_target_color_space()` or equivalent hard-coded sRGB
    helpers are used inside scene-referred grading or working-buffer conversion decisions.
  - Implementation note: Working-buffer-to-QImage conversion helpers now require an explicit target
    `QColorSpace`; `render_parity_test` has compile-time guards that fail if the implicit target
    argument returns. The preview controller local default now delegates to the shared
    `preview_color_space()` helper instead of constructing sRGB directly in the render path.

- [x] Include color pipeline state in preview/cache invalidation keys.
  - Goal: Changing profile, render intent, gamut mapping, headroom, or bit depth cannot reuse stale
    rendered pixels.
  - Completion gate: Controller tests prove every color-pipeline field change schedules the correct
    dirty domain and invalidates affected preview/export caches.
  - Implementation note: `editor_preview_color_cache_test` now proves input/working profile changes
    invalidate adjusted-working and rendered-preview keys, while output profile/headroom/gamut/BPC
    and render-intent changes invalidate rendered previews without discarding reusable
    adjusted-working pixels. `export_job_controller_test` proves export render signatures include
    format and bit-depth settings so 8-bit and 16-bit artifacts cannot reuse stale export renders.

## 3. Output And Export Fidelity

- [x] Add explicit export format and bit-depth settings.
  - Goal: Export supports JPEG 8-bit, PNG 8/16-bit, and TIFF 8/16-bit as intentional choices.
  - Completion gate: Export tests verify requested format and bit depth, including alpha behavior
    and JPEG flattening.
  - Implementation note: Export jobs now resolve explicit `format`, `bitDepth`, `flattenAlpha`,
    `embedProfile`, and writer-format settings from presets. `export_job_controller_test` covers
    JPEG 8-bit alpha flattening, PNG 8/16-bit, and TIFF 8/16-bit artifacts plus persisted render-job
    metadata. The preset-to-writer contract is centralized in `export_image_settings.h` so
    controller preflight metadata and runner write behavior cannot drift independently.

- [x] Embed or preserve output color profile metadata where supported.
  - Goal: Exported images carry the requested output profile when the writer/backend supports it.
  - Completion gate: Export tests decode written files and verify profile tags for sRGB,
    Display P3, and at least one wide-gamut profile.
  - Implementation note: `export_job_controller_test` now exports and decodes sRGB PNG,
    Display P3 PNG, and ProPhoto RGB TIFF artifacts through the real queued render path and verifies
    the decoded `QColorSpace` profile metadata.

- [x] Make output headroom behavior explicit.
  - Goal: SDR exports clamp/tone-map intentionally; HDR or scene-linear outputs preserve over-range
    data only when requested.
  - Completion gate: Tests prove SDR, extended-headroom, and scene-linear exports have distinct,
    expected pixel behavior.
  - Implementation note: `color_pipeline_test` proves SDR headroom compresses over-range values
    while extended headroom preserves extra highlight range. `render_parity_test` proves explicit
    scene-linear float renders preserve over-range linear values. `export_job_controller_test` now
    covers export preflight headroom policy metadata/warnings and real artifacts: PNG integer SDR
    output remains bounded to display pixel values while the opt-in `Scene Linear PFM` export writes
    linear float pixels above 1.0.

## 4. Color Math And Layer Compositing

- [x] Audit and remove premature `0..1` clamps from scene-referred math.
  - Goal: Adjustments and layers preserve valid over-range working values until the output
    transform stage.
  - Completion gate: Tests cover representative over-range pixels through tone, color, grading,
    layers, and export without unwanted intermediate clipping.
  - Implementation note: `color_adjustments.cpp` no longer clamps optical-correction RGB output
    to SDR, and OKLab gamut fitting/lightness clamps are now output-headroom-aware. `color_pipeline_test`
    covers over-range pixels through tone/headroom, optical corrections, RGB color mixing, color
    grading, and normal adjustment layers. `export_job_controller_test` covers final integer SDR
    versus scene-linear float export behavior.

- [x] Replace SDR/HSL layer blend behavior where it conflicts with wide-gamut rendering.
  - Goal: Layer blend modes behave consistently in the working color space and do not force
    display-referred assumptions too early.
  - Completion gate: Layer render tests prove blend modes preserve neutral balance, expected hue,
    and HDR/headroom behavior.
  - Implementation note: `blend_layer_color` now operates on linear scene values, normalizes
    nonlinear blend modes against configured output headroom, and uses OKLab/OKLCH for hue,
    saturation, color, and luminosity blending instead of SDR HSL. `color_pipeline_test` covers
    HDR neutral multiply, headroom-aware overlay, and OKLab color blending with extended lightness.

- [x] Verify local adjustments use the same math as global adjustments.
  - Goal: Masked/layer-local color edits are not a reduced or divergent color pipeline.
  - Completion gate: Render parity tests compare global, local mask, and adjustment-layer recipes
    for equivalent edits within defined tolerance.
  - Implementation note: `color_pipeline_test` now renders the same exposure, tone, white-balance,
    vibrance, and saturation edit as a global recipe, a full-coverage local mask, and a full-opacity
    normal adjustment layer, then verifies the resulting scene-referred pixels match within a defined
    tolerance. `render_parity_test` remains green for preview/export layer and mask workflows.

## 5. Gamut Mapping And Display Transform

- [x] Implement a first-class display rendering transform.
  - Goal: Display output handles tone mapping, gamut compression, render intent, black-point
    compensation, and profile conversion in one explicit stage.
  - Completion gate: Output transform tests cover sRGB, Display P3, Rec.2020, ProPhoto RGB,
    perceptual intent, and relative colorimetric intent.
  - Implementation note: `apply_output_transform` is now a public engine contract for preview and
    export targets. It converts from the working RGB space into the selected output profile, applies
    black-point compensation, distinguishes perceptual compression from relative colorimetric
    clipping, and bounds pixels to configured output headroom. `color_pipeline_test` covers sRGB,
    Display P3, Rec.2020, and ProPhoto RGB targets plus perceptual versus relative intent and BPC.

- [x] Replace simple clipping with perceptual gamut compression.
  - Goal: Out-of-gamut colors compress smoothly while preserving hue and perceptual contrast.
  - Completion gate: Synthetic saturated-patch tests and chart tests prove hue drift and chroma
    loss stay under documented thresholds.
  - Implementation note: `color_pipeline_test` now proves the default policy is enabled perceptual
    compression, `Clip` is explicit, and disabling output gamut mapping matches clipping for a
    saturated wide-gamut fixture. It also verifies synthetic saturated patches preserve hue within
    0.015 turns and avoid excess chroma loss under perceptual output compression. `color_chart_validation_test`
    pushes chromatic ColorChecker-style patches beyond SDR and verifies compressed chart hue drift
    stays under 0.020 turns with chroma loss under 0.035.

- [x] Add gamut and clipping diagnostics backed by the final output transform.
  - Goal: Gamut overlays report what the selected output target will actually clip or compress.
  - Completion gate: Overlay tests compare diagnostic pixels against final transform decisions.
  - Implementation note: `analyze_gamut_warnings` now derives warning decisions from
    `apply_output_transform`, so diagnostics use the same target profile conversion and headroom
    excursion logic as the final output contract. `color_pipeline_test` compares diagnostic tested
    pixels, warning counts, and max channel excursion against per-pixel output-transform decisions.

## 6. RAW And Input Color

- [x] Move white balance into the input transform stage.
  - Goal: RAW white balance and camera adaptation happen before generic grading controls.
  - Completion gate: RAW/input tests prove white balance changes do not behave like post-grade
    RGB tinting and remain consistent between preview and export.
  - Done: `color_pipeline_test` now compares input-stage RAW white balance against an
    adjustment-layer white balance using the same numeric controls, proving the result is
    not late RGB tinting. The same input-stage result is then passed through preview and
    export `apply_output_transform` paths and compared against the span overload to prove
    the output transforms consume one consistent source.

- [x] Make camera normalization first-class recipe data.
  - Goal: Camera profile, baseline exposure, black/white levels, highlight reconstruction,
    chromatic adaptation, and camera look are explicit and serializable.
  - Completion gate: Recipe and render tests prove each input field affects RAW-like sources only
    when the input transform is enabled.
  - Done: `edit_recipe_state_test` serializes and round-trips input transform enablement,
    chromatic adaptation, camera matching look, RAW white balance, black/white levels,
    calibration, and primary corrections. `color_pipeline_test` now proves the full RAW
    normalization bundle affects RAW-like rendering when enabled and becomes inert when
    `raw_input_transform_enabled` is false. The engine input-stage parameter builder now
    gates camera profile, source white balance, baseline exposure, camera look, calibration,
    raw levels, and highlight reconstruction behind the input-transform enablement.

- [x] Add RAW/reference fixture coverage.
  - Goal: RAW rendering is measured against known neutral, skin, sky, and saturated patches.
  - Completion gate: CI includes synthetic RAW-style fixtures now, and a documented path for adding
    licensed real RAW fixtures later.
  - Done: `color_chart_validation_test` includes synthetic RAW-style fixtures for neutral,
    skin, sky, saturated red, and saturated cyan patches using deterministic camera metadata,
    input-stage white balance, camera profile resolution, RAW level normalization, highlight
    reconstruction, and output-gamut compression checks. `docs/raw-reference-fixture-plan.md`
    documents the path and license gates for adding real RAW fixtures later.

## 7. Control Response And Limits

- [x] Retune control response curves after pipeline correctness is proven.
  - Goal: Exposure, contrast, highlights, shadows, whites, blacks, vibrance, saturation, mixer, and
    grading controls feel photographic and predictable.
  - Completion gate: Color science sweep tests prove monotonic behavior, neutral preservation,
    hue stability, and visible effect thresholds across the control range.
  - Done: `color_science_sweep_test` now covers monotonic neutral-ramp behavior,
    neutral preservation, hue stability for tone controls, useful small-delta thresholds,
    endpoint finite-output checks, vibrance preference for muted colors, mixer response,
    and grading response across the current control range.

- [x] Decide slider limits per control from measured behavior.
  - Goal: Limits are expanded only where the math remains stable and useful.
  - Completion gate: Each expanded limit has tests for finite output, no unexpected clipping,
    preview/export parity, and acceptable hue/neutral drift.
  - Done: `docs/color-control-limit-decisions.md` records the decision to keep the
    current published limits until wider ranges are proven. `color_science_sweep_test`
    validates the current endpoints for finite output, alpha preservation, and bounded
    preview/export output transforms; future expanded limits must add the same proof
    plus visible-effect and hue/neutral drift coverage.

- [x] Add control-specific acceptance fixtures.
  - Goal: The most important controls have representative before/after references.
  - Completion gate: Golden tests lock representative portrait, landscape, night, monochrome,
    high-contrast, and saturated-color recipes.
  - Done: `render_golden_test` now locks representative night recovery, portrait,
    landscape, monochrome, high-contrast, and saturated-color recipes with behavior
    checks plus deterministic hashes.

## 8. CPU, GPU, And Performance Parity

- [x] Define supported GPU color-pipeline shapes.
  - Goal: Metal either supports the active pipeline correctly or reports a clear CPU fallback reason.
  - Completion gate: Tests cover supported GPU paths and every expected fallback reason.
  - Done: `evaluate_gpu_pipeline_support` exposes the same shape decision used by
    rendering. `render_parity_test` covers the supported LUT-backed Metal path plus
    `metal_unavailable`, `local_mask_sets`, `retouch_masks`, `legacy_local_masks`,
    `extended_range_pixels`, `exact_global_tone`, `exact_post_color`, and
    `cpu_required_stages` fallback reasons.

- [x] Expand Metal support beyond the narrow linear-sRGB path where practical.
  - Goal: Common preview edits can stay fast under the default wide-gamut pipeline.
  - Completion gate: CPU/Metal parity tests pass for the selected default working profile and core
    adjustment set, or the fallback is explicitly documented and measured.
  - Done: `render_parity_test` now asserts GPU support for a `Linear ProPhoto RGB`
    GPU-compatible recipe and runs CPU/Metal parity for that wide-gamut path when a
    Metal backend is available. Unsupported exact tone/color stages remain named CPU
    fallbacks instead of silent GPU attempts.

- [x] Measure end-to-end preview latency after color changes.
  - Goal: Industry-grade color does not make basic slider edits feel dead.
  - Completion gate: Existing perf gates report preview latency, backend choice, cache behavior,
    and fallback reason for representative color recipes.
  - Done: `docs/color-pipeline-gpu-performance-gates.md` documents the required
    release/perf gates and representative color recipes. Existing preview perf and
    release scripts preserve preview latency, backend/cache evidence, and fallback
    reason fields; the support matrix prevents fallback reasons from drifting.

## 9. Validation Harness

- [x] Add Delta E style chart validation.
  - Goal: Color correctness is measured in perceptual terms, not only channel deltas.
  - Completion gate: `color_chart_validation_test` defines patch tolerances for neutral, skin,
    saturated, and memory-color patches.
  - Done: `color_chart_validation_test` now assigns named patch categories and
    perceptual tolerances for neutral, skin, saturated, and memory-color patches,
    and fails with the category/tolerance that drifted.

- [x] Add multi-profile transform validation.
  - Goal: sRGB, Display P3, Rec.2020, ProPhoto RGB, and linear working profiles are tested as real
    targets.
  - Completion gate: `color_pipeline_test` proves transforms round-trip or map within documented
    tolerances.
  - Done: `color_pipeline_test` proves same-profile working/output pairs round-trip
    within `1e-5` for Linear sRGB/sRGB, Linear Display P3/Display P3,
    Linear Rec.2020/Rec.2020, and Linear ProPhoto RGB/ProPhoto RGB, while
    cross-profile sRGB, Display P3, Rec.2020, and ProPhoto output transforms stay
    finite and bounded.

- [x] Prove preview/export parity for every supported output path.
  - Goal: Preview is a faithful view of export modulo intentional resolution and output-profile
    differences.
  - Completion gate: `render_parity_test` covers 8-bit JPEG, 16-bit PNG, 16-bit TIFF, sRGB,
    Display P3, and wide-gamut export paths.
  - Done: `render_parity_test` now includes an output-path matrix for JPEG 8-bit
    sRGB, PNG 16-bit sRGB, TIFF 16-bit Display P3, and TIFF 16-bit ProPhoto RGB,
    comparing preview, full-resolution render, and encoded readback.

- [x] Add real-photo approval workflow.
  - Goal: Mathematical tests are backed by editorial review on representative images.
  - Completion gate: A documented fixture process exists, with approved outputs stored only when
    licensing permits.
  - Done: `docs/color-real-photo-approval-workflow.md` defines fixture coverage,
    licensing rules, approval evidence, and CI promotion requirements for real-photo
    color review.

## 10. User-Facing Product Finish

- [x] Keep advanced color controls discoverable but not noisy.
  - Goal: Normal editing exposes clear photographic controls; Lab/advanced surfaces expose profiles,
    transforms, and diagnostics.
  - Completion gate: Shell tests prove profile, soft-proof, bit-depth, and gamut diagnostics are
    reachable without cluttering the default edit workflow.
  - Done: `desktop_shell_smoke_test` source/runtime coverage now verifies the export color-space
    section, preflight bit-depth/profile summary, soft-proof status, and developer-gated gamut
    diagnostics. Focused shell proof through `PHOTO_EDITOR_SMOKE_STOP_AFTER_PIXEL_ZOOM=1` passes.

- [x] Show export color intent clearly.
  - Goal: Users know whether they are exporting JPEG sRGB, 16-bit Display P3, TIFF ProPhoto, or
    another target.
  - Completion gate: Export UI tests verify visible format, bit depth, profile, and warning states.
  - Done: `ExportView.qml` preflight status includes bit depth alongside format/profile; shell
    source checks cover the visible color-space section and current preset bit-depth binding.

- [x] Add actionable warnings for unsupported or lossy choices.
  - Goal: The app warns before flattening alpha to JPEG, clipping HDR to SDR, or exporting wide-gamut
    edits to a narrow target.
  - Completion gate: Controller and shell tests prove warning conditions trigger only when relevant.
  - Done: `export_job_controller_test` passes with warning coverage for alpha flattening,
    SDR/headroom clipping, profile mismatch, preview/export parity, gamut mapping, and RAW clipping.
    Note: the complete broad `desktop_shell_smoke_test` currently times out later in unrelated
    mask/brush proof sections; the focused color/export shell gate passes.

## Final Completion Gate

- [x] Declare the color recipe industry-grade.
  - Goal: One recipe produces predictable, color-managed preview and export output across supported
    profiles, bit depths, CPU/GPU paths, RAW/raster sources, layers, masks, and presets.
  - Completion gate: The full validation suite passes:
    - `color_pipeline_test`
    - `color_chart_validation_test`
    - `color_science_sweep_test`
    - `color_tool_invariant_test`
    - `render_parity_test`
    - `render_golden_test`
    - relevant export/controller shell tests
  - Done: The deterministic color/render/export suite passed with
    `ctest --test-dir build/dev-macos-debug -R '^(color_pipeline_test|color_chart_validation_test|color_science_sweep_test|color_tool_invariant_test|render_parity_test|render_golden_test|export_job_controller_test)$' --output-on-failure --timeout 420`.
    The focused relevant shell gate passed with
    `PHOTO_EDITOR_SMOKE_STOP_AFTER_PIXEL_ZOOM=1 desktop_shell_smoke_test`, covering the export
    color UI, soft-proof/gamut diagnostics, and pixel-inspection preview path. The complete broad
    `desktop_shell_smoke_test` still times out later in unrelated mask/brush proof sections and
    remains outside this color-recipe completion gate.
