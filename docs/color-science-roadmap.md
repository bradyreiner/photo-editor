# Color Science Roadmap

## Goal

Turn the current photo pipeline into a color-managed render architecture that can compete on trust,
not just control count.

The target pipeline is:

`camera input -> scene-linear wide gamut working space -> grading -> display/export transform`

## Current constraints in code

The repo already has useful building blocks:

- a named render graph in `engine/src/pipeline/render_graph.cpp`
- linear-float image handling in `apps/desktop/src/working_image.h`
- RAW decode integration in `apps/desktop/src/raw_image_decoder.mm`
- global and local tone/color math in `engine/src/pipeline/color_adjustments.cpp`
- a basic output color-space toggle in desktop recipe state and render helpers

The main gaps are structural:

- the canonical working space is still `QColorSpace::SRgbLinear`
- output color management is effectively `sRGB` or `Display P3`
- RAW normalization is mostly delegated to `CIRAWFilter` without a first-class input transform model
- white balance is still a slider heuristic instead of a proper camera/input adaptation stage
- export is still photo-JPEG oriented rather than profile- and bit-depth-aware
- there is no color-validation harness based on reference charts or Delta E thresholds

## Guiding rules

1. Keep preview and export on the same recipe-driven render path.
2. Separate scene-referred grading from display-referred output transforms.
3. Treat color spaces, transfer functions, and render intents as explicit state, not incidental Qt conversions.
4. Keep the engine deterministic and testable without the UI.
5. Ship validation with each milestone so color behavior is measured rather than eyeballed.

## Proposed architecture

### New engine types

Add a dedicated color pipeline model under `engine/include/photo_editor/pipeline/`.

- `color_space.h`
  - `enum class ColorPrimaries`
  - `enum class TransferFunction`
  - `enum class WhitePoint`
  - `struct RgbColorSpace`
- `color_transform.h`
  - `struct Matrix3x3`
  - `struct ChromaticAdaptationOptions`
  - helpers for RGB/XYZ conversion and adaptation
- `color_pipeline.h`
  - `enum class InputTransformKind`
  - `enum class WorkingTransformKind`
  - `enum class OutputTransformKind`
  - `enum class RenderIntent`
  - `struct InputColorProfile`
  - `struct WorkingColorProfile`
  - `struct OutputColorProfile`
  - `struct ColorPipelineConfig`
- `color_pipeline.cpp`
  - input-to-working transforms
  - working-to-output transforms
  - gamut mapping and tone-mapping boundaries

### Recipe model changes

The desktop recipe should stop storing color state as a single `output_color_space` string and instead
persist a structured color pipeline section.

Suggested additions in `apps/desktop/src/edit_recipe_state.h`:

- `input_profile_name`
- `working_profile_name`
- `output_profile_name`
- `preview_output_profile_name`
- `render_intent`
- `highlight_reconstruction_mode`
- `chromatic_adaptation_method`
- `raw_input_transform_enabled`

Longer term this should become a nested object rather than more flat scalar fields:

- `raw_adjustments`
- `grading`
- `color_pipeline`
- `output_transform`

### Raw/input normalization boundary

Split the current `raw_decode` and `camera_lens_normalization` stages into explicit input responsibilities.

- `raw_decode`
  - decode sensor/raster source into a high precision source buffer
- `input_transform`
  - camera profile
  - white balance adaptation
  - highlight reconstruction
  - source-to-working transform
- `camera_lens_normalization`
  - lens shading
  - distortion
  - chromatic aberration
  - geometry normalization

## Primary implementation targets

### Engine

- `engine/include/photo_editor/pipeline/color_adjustments.h`
- `engine/src/pipeline/color_adjustments.cpp`
- `engine/src/pipeline/render_graph.cpp`
- new:
  - `engine/include/photo_editor/pipeline/color_space.h`
  - `engine/include/photo_editor/pipeline/color_transform.h`
  - `engine/include/photo_editor/pipeline/color_pipeline.h`
  - `engine/src/pipeline/color_transform.cpp`
  - `engine/src/pipeline/color_pipeline.cpp`

### Desktop shell

- `apps/desktop/src/working_image.h`
- `apps/desktop/src/raw_image_decoder.h`
- `apps/desktop/src/raw_image_decoder.mm`
- `apps/desktop/src/photo_render_utils.cpp`
- `apps/desktop/src/editor_preview_controller.h`
- `apps/desktop/src/editor_preview_controller.cpp`
- `apps/desktop/src/edit_recipe_state.h`
- `apps/desktop/qml/workspaces/EditView.qml`

### Tests

- `tests/editor_preview_math_test.cpp`
- new:
  - `tests/color_pipeline_test.cpp`
  - `tests/color_transform_test.cpp`
  - `tests/raw_input_transform_test.cpp`
  - `tests/output_transform_test.cpp`

## Milestone 1: Color Pipeline Core

### Objective

Introduce first-class color pipeline configuration and remove the assumption that linear sRGB is the
only working space.

### Work items

- add the new engine color types and transform helpers
- define supported spaces for v1:
  - input: source embedded profile, camera native placeholder, sRGB, Display P3
  - working: wide-gamut scene-linear profile
  - output: sRGB, Display P3, Adobe RGB, ProPhoto RGB, Rec.709
- thread `ColorPipelineConfig` from recipe state into preview and export
- update `working_image.h` so working-space conversion is no longer hard-coded to `SRgbLinear`
- keep the existing tone/color math working inside the new working-space contract

### Definition of done

- preview render path accepts explicit working and output profiles
- export render path accepts the same config
- no implicit fallback from working space to preview space inside core engine logic

## Milestone 2: RAW Input And Camera Normalization

### Objective

Make RAW rendering dependable enough that users trust the base image before grading.

### Work items

- introduce `InputColorProfile` and `RawInputTransformSettings`
- move white balance adaptation logic out of the generic grading stage and into the input stage
- replace the current slider-only white balance heuristic with chromatic adaptation options
- add highlight reconstruction modes to recipe state and RAW decode settings
- preserve current `CIRAWFilter` integration as one backend, but stop letting it define the app's full color model
- reserve room for future camera-specific profile data and dual-illuminant behavior

### Definition of done

- RAW decode and raster decode both terminate in a clearly defined working-space buffer
- white balance behavior is consistent between preview and export
- highlight reconstruction and raw profile settings are visible in recipe state

## Milestone 3: Output Transform And Export Fidelity

### Objective

Make output behavior explicit and professional enough for proofing and delivery.

### Work items

- replace the current `output_color_space` string switch with structured output transform selection
- add ICC-aware output conversion boundaries in preview and export helpers
- add 16-bit TIFF and PNG export options alongside JPEG
- embed output profiles where the writer/backend supports it
- separate preview output profile from export output profile
- keep crop, geometry, and color transforms identical between preview and export

### Definition of done

- export supports at least JPEG, PNG, and TIFF with profile-aware output transforms
- preview can target a display profile independently of export target
- output transforms are not mixed into scene-referred grading math

## Milestone 4: Validation And Regression Harness

### Objective

Turn color quality into something the repo can measure continuously.

### Work items

- add synthetic neutral-ramp and saturated-patch tests for transform correctness
- add gray balance and neutral preservation tests for LUT and non-LUT paths
- add CPU and Metal parity tests for core transforms and gamut handling
- add fixture-based reference tests for chart patches and highlight rolloff
- define tolerances:
  - transform parity threshold
  - neutral-channel drift threshold
  - Delta E threshold for reference patches
- document how to expand the validation corpus over time

### Definition of done

- CI fails on meaningful color regressions
- there is a documented color-validation suite rather than ad hoc visual checks

## Proposed first-pass file-level backlog

1. Add engine color-space primitives and transform math.
   - Files:
     - `engine/include/photo_editor/pipeline/color_space.h`
     - `engine/include/photo_editor/pipeline/color_transform.h`
     - `engine/src/pipeline/color_transform.cpp`
2. Add pipeline configuration objects and render boundaries.
   - Files:
     - `engine/include/photo_editor/pipeline/color_pipeline.h`
     - `engine/src/pipeline/color_pipeline.cpp`
     - `engine/src/pipeline/render_graph.cpp`
3. Thread structured color pipeline state through desktop recipe and preview.
   - Files:
     - `apps/desktop/src/edit_recipe_state.h`
     - `apps/desktop/src/editor_preview_controller.h`
     - `apps/desktop/src/editor_preview_controller.cpp`
     - `apps/desktop/src/photo_render_utils.cpp`
4. Remove the fixed working-space assumption from desktop image conversion helpers.
   - Files:
     - `apps/desktop/src/working_image.h`
5. Refactor RAW settings around input transform concerns.
   - Files:
     - `apps/desktop/src/raw_image_decoder.h`
     - `apps/desktop/src/raw_image_decoder.mm`
6. Move white balance and highlight reconstruction to the input stage.
   - Files:
     - `engine/include/photo_editor/pipeline/color_adjustments.h`
     - `engine/src/pipeline/color_adjustments.cpp`
7. Expand export targets and output transforms.
   - Files:
     - `apps/desktop/src/job_runner.cpp`
     - `apps/desktop/src/photo_render_utils.cpp`
8. Add dedicated color pipeline tests.
   - Files:
     - `tests/color_pipeline_test.cpp`
     - `tests/color_transform_test.cpp`
     - `tests/output_transform_test.cpp`

## 7-week execution sequence

### Week 1: pipeline contract and state

- add `ColorPipelineConfig` and enums
- add recipe serialization placeholders
- update render graph stage naming to include `input_transform`
- keep behavior functionally equivalent while introducing structure

### Week 2: transform math and working space

- implement RGB/XYZ matrices and chromatic adaptation
- introduce wide-gamut scene-linear working profile support
- remove `SRgbLinear` as the only canonical working-space assumption

### Week 3: preview path conversion

- thread working/output profile state into preview render
- update `working_image.h` and `photo_render_utils.cpp`
- verify preview parity against the existing pipeline before widening defaults

### Week 4: RAW input refactor

- add `RawInputTransformSettings`
- move white balance and highlight reconstruction boundaries
- keep `CIRAWFilter` as a decode backend, not the whole input model

### Week 5: export/output overhaul

- add structured output transform handling
- add PNG/TIFF plus higher bit depth support where supported
- embed output profile metadata

### Week 6: validation harness

- add transform tests, neutral tests, and output regression tests
- add CPU/Metal parity checks for the new transform boundary

### Week 7: default tuning and rollout

- choose defaults for working and output profiles
- tune gamut mapping and highlight rolloff under the new contract
- document unsupported cases and future camera-profile expansion

## Recommended defaults for the first rollout

- working space: one wide-gamut scene-linear profile
- preview output: sRGB by default, Display P3 when explicitly selected
- export output: user-selectable with sRGB as default
- render intent: perceptual default, relative colorimetric later if needed

The important part is not which wide-gamut profile name ships first. The important part is making the
pipeline explicit enough that it can evolve without rewriting every edit operator later.

## Success test

This roadmap is working when the app can:

1. decode a RAW or raster image into a known working space
2. apply the same grade in preview and export with predictable behavior
3. target more than one real output profile without changing grading math
4. pass chart, neutral, and parity tests without manual inspection
5. expand camera/profile support without redesigning the recipe model
