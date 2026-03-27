# Editor Upgrade Plan

## Goal

Upgrade the editor from its current preview-oriented scaffold into a professional editing surface with:

- real point curves
- stronger color grading
- Photoshop-style masking and selection workflows
- a real crop and straighten tool
- guide overlays
- text and annotation overlays

This plan assumes the current codebase structure remains intact:

- `apps/desktop`: QML and controller integration
- `engine`: render graph, edit math, image processing
- `rust`: catalog and recipe persistence utilities

## Current constraints in code

The current implementation is limited by data shape more than UI polish.

- Curve state is fixed to four scalar controls.
- Masking is a single linear mask with a few sliders.
- Crop state is only `preset + zoom` with a centered crop.
- Guides, text, and overlays do not exist as first-class objects.
- The persisted recipe does not yet model rich edit state.

Because of that, the first milestone is a model refactor. Every other feature depends on it.

## Guiding rules

1. Keep edits nondestructive and recipe-driven.
2. Keep QML focused on interaction and presentation, not image math.
3. Keep render behavior deterministic and testable in `engine`.
4. Use the same recipe and render path for preview and export.
5. Add structure once, then build tools on top of it. Do not add one-off controller properties for every new tool.

## Delivery order

1. Recipe and state refactor
2. Point curves
3. Crop and guides
4. Mask stack core
5. Brush, radial, and range masks
6. Selection tools and refinement
7. Text and overlay objects
8. Export parity, tests, and performance

## Milestone 0: Recipe And State Refactor

### Objective

Replace the current scalar-heavy edit state with structured recipe objects that can support the full editor roadmap.

### Files to change

- `engine/include/photo_editor/pipeline/edit_recipe.h`
- `engine/include/photo_editor/pipeline/color_adjustments.h`
- `apps/desktop/src/editor_preview_controller.h`
- `apps/desktop/src/editor_preview_controller.cpp`
- `rust/crates/catalog/src/bin/save_edit_recipe.rs`
- recipe parsing and serialization call sites in desktop and catalog code

### New data model

Add first-class recipe sections instead of individual controller-only fields.

- `global_adjustments`
- `curves`
- `color_grading`
- `crop_transform`
- `mask_stack`
- `selection_state` as transient UI state only, not persisted by default
- `overlay_items`
- `guide_settings`

### Suggested structures

- `CurvePoint`
  - `x`
  - `y`
  - `type`
  - optional handle data if Bezier is adopted
- `CurveChannel`
  - `channel`
  - `points[]`
- `CropTransform`
  - `x`
  - `y`
  - `width`
  - `height`
  - `rotation_degrees`
  - `straighten_degrees`
  - `aspect_ratio`
  - `flip_horizontal`
  - `flip_vertical`
- `MaskNode`
  - `id`
  - `kind`
  - `operation`
  - `enabled`
  - `feather`
  - `opacity`
  - `geometry`
  - `range_constraints`
  - `brush_strokes`
  - `local_adjustments`
- `OverlayItem`
  - `id`
  - `kind`
  - `position`
  - `size`
  - `rotation`
  - `style`
  - `text_content`

### Work items

- Add versioned recipe structs in `engine`.
- Replace direct scalar serialization with structured JSON serialization.
- Refactor `EditorPreviewController` to expose model-backed state instead of one property per future tool.
- Keep compatibility with the existing recipe version by adding migration/defaulting logic.
- Update preview cache fingerprinting so structured edits invalidate correctly.

### Acceptance criteria

- Existing images still load with old recipes.
- New recipe JSON can represent curves, crop geometry, masks, and overlays.
- Preview cache changes whenever a nested edit object changes.
- No feature after this milestone needs a new ad hoc JSON field pattern.

## Milestone 1: Real Point Curves

### Objective

Replace the current parametric tone curve with a Photoshop-style point curve system.

### Files to change

- `apps/desktop/qml/components/ToneCurveEditor.qml`
- `apps/desktop/qml/workspaces/EditView.qml`
- `engine/include/photo_editor/pipeline/color_adjustments.h`
- `engine/src/pipeline/color_adjustments.cpp`
- tests under `tests/`

### Scope

- Arbitrary point insertion by click
- Point dragging in both axes
- Point deletion
- Endpoint locking at `(0,0)` and `(1,1)`
- Channel switching: `Master`, `Red`, `Green`, `Blue`
- Histogram overlay behind the curve
- Keyboard deletion for selected interior points

### Engine design

- Build a curve evaluator backed by a LUT.
- Generate a 256 or 1024 entry LUT per channel for preview.
- Use monotone cubic interpolation first.
- Defer full Bezier handles unless monotone cubic feels insufficient.
- Apply master curve to luminance or unified RGB mapping, then apply per-channel curves.

### UI design

- Replace the current fixed handles with editable control points.
- Support hover, selected point state, and channel tabs.
- Add numeric point readouts for input and output.
- Keep the graph square and support histogram underlay.

### Acceptance criteria

- Users can add custom points anywhere along the curve.
- Curves are smooth and visually stable while dragging.
- Per-channel curves visibly affect the image.
- Reopening an image restores all curve points exactly.
- Curve behavior matches export output.

## Milestone 2: Crop, Straighten, And Guides

### Objective

Replace preset-and-zoom crop with a real crop box and geometry controls.

### Files to change

- `apps/desktop/qml/workspaces/EditView.qml`
- new crop overlay component under `apps/desktop/qml/components/`
- `apps/desktop/src/editor_preview_controller.h`
- `apps/desktop/src/editor_preview_controller.cpp`
- crop math in `engine`

### Scope

- Crop box with corner and edge handles
- Drag to reposition crop
- Custom aspect ratios
- Presets such as `Original`, `Square`, `4x5`, `16x9`, `3x2`
- Straighten slider
- Rotation support
- Flip horizontal and vertical
- Dimmed outside area
- Guide overlays

### Guide overlay support

- Rule of thirds
- Center lines
- Diagonal
- Golden ratio
- Optional custom guide lines later

### Engine design

- Replace centered crop math with explicit crop rectangle math.
- Separate crop geometry from final transform.
- Keep crop evaluation in the `crop_transform` render stage.
- Ensure crop is applied identically in preview, export, and carousel reuse paths.

### Acceptance criteria

- Users can directly manipulate the crop box on canvas.
- Straighten rotates the preview around the crop box correctly.
- Guides can be toggled without changing the recipe unless they are user-created custom guides.
- Export matches preview crop exactly.

## Milestone 3: Mask Stack Core

### Objective

Replace the single linear mask implementation with a real stack of mask items and per-mask adjustments.

### Files to change

- `apps/desktop/qml/workspaces/MaskView.qml`
- `apps/desktop/qml/workspaces/EditWorkspace.qml`
- new QML components for mask list and mask controls
- `apps/desktop/src/editor_preview_controller.h`
- `apps/desktop/src/editor_preview_controller.cpp`
- `engine/include/photo_editor/pipeline/edit_recipe.h`
- `engine/include/photo_editor/pipeline/color_adjustments.h`
- `engine/src/pipeline/color_adjustments.cpp`

### Mask types in this milestone

- linear gradient
- radial gradient
- brush

### Stack behavior

- add mask
- duplicate mask
- rename mask
- enable or disable mask
- reorder masks
- delete mask
- per-mask local adjustments

### Local adjustments per mask

- exposure
- contrast
- highlights
- shadows
- whites
- blacks
- temperature
- tint
- texture
- clarity
- dehaze
- saturation

### Engine design

- Build mask rasterization into a mask buffer.
- Composite mask nodes in stack order.
- Apply local adjustments after global tone and before crop/output.
- Keep mask evaluation resolution-aware so interactive previews can use reduced resolution.

### Acceptance criteria

- Multiple masks can exist on one image.
- Each mask has independent local adjustments.
- Enabling, disabling, and reordering masks updates preview correctly.
- Mask overlays are readable and consistent with the app's cool accent system.

## Milestone 4: Range Masks And Combine Operations

### Objective

Add the workflows that make masking feel professional rather than basic.

### Scope

- luminance range mask
- color range mask
- add
- subtract
- intersect
- invert
- density
- feather
- mask visualization modes

### Engine design

- Range masks should operate from image analysis, not UI-only approximations.
- Color range should use perceptual distance, ideally in OKLab or OKLCh.
- Luminance range should offer smooth falloff with user-controlled softness.

### UI design

- Mask row badges should show operation type.
- Add mask preview modes:
  - overlay
  - grayscale mask
  - black on white
  - white on black

### Acceptance criteria

- Users can build one mask from multiple components.
- Subtract and intersect behave predictably.
- Color and luminance range masks feel stable on real photos.

## Milestone 5: Selection Tools

### Objective

Add Photoshop-like selection tools that can create or refine masks.

### Scope

- rectangular marquee
- elliptical marquee
- freehand lasso
- polygonal lasso
- color wand
- edge-aware refine

### Phase split

Phase 5A

- rectangle
- ellipse
- lasso
- polygon lasso

Phase 5B

- color wand
- contiguous / global toggle
- tolerance
- edge refine

Phase 5C

- AI-assisted subject
- AI-assisted sky
- AI-assisted object

### Design rules

- Selections are temporary until committed into a mask.
- Users can convert a selection into:
  - new mask
  - add to current mask
  - subtract from current mask
  - intersect current mask

### Acceptance criteria

- Selections are visible on canvas with clear handles and outlines.
- Selection-to-mask conversion is one click.
- Refinement improves edges on hair, clothing, and high-contrast boundaries.

## Milestone 6: Brush Masking Refinement

### Objective

Make brush masking feel accurate and controllable on real images.

### Scope

- brush size
- feather
- flow
- density
- hardness if brush model requires it
- erase mode
- auto-mask edge detection
- stroke smoothing
- pressure input support later

### Engine design

- Store brush strokes as vector stroke data, not only flattened bitmaps.
- Rasterize strokes into the current preview resolution for interaction.
- Support replay at export resolution.

### Acceptance criteria

- Brush edits remain editable after reopen.
- Auto-mask improves edge adherence without lagging badly.
- Export output matches on-screen brush masks.

## Milestone 7: Text, Guides, And Overlay Objects

### Objective

Add non-destructive overlays for text and composition aids without mixing them into image color edits.

### Files to change

- new overlay scene components in `apps/desktop/qml/components/`
- `apps/desktop/qml/workspaces/EditView.qml`
- `apps/desktop/src/editor_preview_controller.*`
- recipe serialization code
- export code paths that need overlay baking options

### Overlay types

- text
- line
- rectangle
- ellipse
- guide line

### Text features

- content
- font family
- size
- weight
- alignment
- fill color
- opacity
- rotation
- bounding box handles

### Guide features

- snap-safe draggable guides
- show or hide guides
- lock guides
- clear all guides

### Export behavior

- preview only
- bake into export
- omit from export

### Acceptance criteria

- Overlay items can be selected and transformed on canvas.
- Text remains editable after save and reopen.
- Guides can be toggled independently from overlay objects.
- Export honors overlay inclusion mode.

## Milestone 8: Color Grading Expansion

### Objective

Go beyond white balance and HSL into more professional color finishing controls.

### Scope

- shadows, midtones, highlights color wheels
- balance control
- blending control
- optional global wheel
- `Hue vs Hue`
- `Hue vs Sat`
- `Hue vs Luma`
- `Luma vs Sat`

### Engine design

- Keep grading in perceptual space where possible.
- Use the existing OKLab groundwork as the base for chroma and hue-aware operations.
- Make wheel controls mask-compatible so any mask can drive local color grading.

### Acceptance criteria

- Color wheels behave predictably across skin, skies, and mixed lighting.
- Curve and grading interactions are stable and do not create obvious hue artifacts.

## Milestone 9: Export Parity, Performance, And Hardening

### Objective

Make the new editor model reliable at scale.

### Work items

- unify preview and export recipe evaluation path
- extend cache fingerprinting for nested state
- add regression tests for recipe round-tripping
- add curve interpolation tests
- add crop geometry tests
- add mask compositing tests
- add performance benchmarks for interactive preview
- add fallback behavior for very large images

### Acceptance criteria

- Preview and export match visually.
- Structured recipe changes are persisted safely.
- Interactive preview remains responsive during drag-heavy tasks.
- Tests cover curve math, crop math, and mask stack evaluation.

## Proposed task breakdown

### Sprint 1

- milestone 0
- milestone 1 engine scaffolding
- milestone 1 QML replacement

### Sprint 2

- milestone 2
- histogram support for curves
- crop overlay interaction polish

### Sprint 3

- milestone 3
- milestone 4 partial

### Sprint 4

- finish milestone 4
- milestone 5A
- milestone 6 basics

### Sprint 5

- milestone 5B
- milestone 7
- milestone 8 basics

### Sprint 6

- milestone 5C if desired
- milestone 8 finish
- milestone 9 hardening

## Risks

- Controller bloat if new tools are added as individual Q_PROPERTY fields instead of structured models
- Export mismatch if preview and export diverge during implementation
- Poor masking performance if brush strokes are only stored as giant bitmaps
- Rework if crop is implemented in UI first without engine-level geometry support
- Recipe churn if versioning and migration are not handled from the start

## Non-negotiable implementation choices

- Do not keep adding scalar edit fields for every new tool.
- Do not flatten brush masks permanently during editing.
- Do not make selections persisted recipe data unless explicitly committed.
- Do not let preview-only geometry differ from export geometry.
- Do not add text and guides as ad hoc paint calls inside image processing code.

## Definition of done for the program

The upgrade is complete when:

- the editor supports real point curves with arbitrary control points
- masking supports multiple masks and combine operations
- crop is directly manipulable on canvas
- guide overlays and text overlays exist as first-class objects
- selection tools can feed mask creation
- preview and export use the same recipe semantics
- recipes round-trip without losing structured edit data
