# Photo Tool Polish Audit

This document is the execution plan for making the existing photo-editing tools feel professional, intuitive, fast, and trustworthy. It intentionally avoids broad feature expansion. The goal is to perfect the tools already present or clearly underway.

## Product Principle

The app should not win by having the longest tool list. It should win because each tool is:

- easy to find
- obvious when active
- precise when needed
- fast enough to feel direct
- accurate between preview and export
- reversible through undo, reset, and history
- usable with keyboard, mouse, trackpad, and tablet
- integrated with masks, layers, presets, recipes, and export

Every tool should feel like it belongs to one coherent editor, not like a separate experiment.

## Capability Verification Gate

Every exposed capability must be audited and tested before it is treated as
finished. The UI pass is not complete until the capability has deterministic
behavior, a coherent interaction model, and automated proof at the right level:
mathematical invariants for pixel/geometry tools, state-machine tests for
workflow tools, render/export parity for visual output, and UI smoke coverage
for the actual user path.

Use [docs/capability-verification-plan.md](/Users/bradyreiner/projects/PersonalProjects/photo-editor/docs/capability-verification-plan.md)
as the required checklist. If a capability cannot pass that gate yet, mark it
experimental, hide it from the primary workflow, or cut it.

## Tool Quality Bar

Use this checklist for every tool before calling it finished.

- [x] Entry point is obvious in the workspace.
- [x] Active state is visually unmistakable.
- [x] Only relevant controls are shown while the tool is active.
- [x] On-canvas handles exist when direct manipulation is expected.
- [x] Slider/value labels match the real render direction.
- [x] Reset restores the correct default without side effects.
- [x] Undo groups continuous drags or brush strokes into useful history steps.
- [x] Before/after works for the tool's result.
- [x] Preview updates quickly enough to guide the user.
- [x] Settled preview matches export within the intended tolerance.
- [x] Tool works correctly with masks/layers/presets where applicable.
- [x] Tool communicates why an action is unavailable.
- [x] Keyboard shortcuts exist for the core workflow.
- [x] Automated tests prove the applicable math, state transitions, render/export parity, and UI path.

Status: This quality bar is enforced by the 100-point backlog below, the duplicate-free capability registry gate in `desktop_shell_smoke_test`, focused controller state-machine coverage, render/export parity tests, and exact math tests for pixel, mask, layer, crop, and transform behavior.

## Priority Order

### 1. Basic Edit Flow

These tools form the first impression. They must feel calm, predictable, and fast before deeper tools matter.

- crop and transform
- light controls
- color controls
- tone curve
- presets
- before/after
- reset and undo

### 2. Local Editing Flow

These tools are powerful but easy to make confusing. They need strong active states, clear stack behavior, and live feedback.

- mask stack
- brush masks
- linear/radial masks
- luminance/color/depth/alpha masks
- smart/object/subject/sky/background/people selections if available locally
- mask refine controls
- overlay modes

### 3. Retouch Flow

Retouch should feel like its own workflow, not like hidden mask settings.

- clone
- heal
- patch
- remove/inpaint fallback
- dust
- red-eye
- skin cleanup
- frequency smoothing
- retouch stroke management

### 4. Layer Flow

Layers should explain the image stack. The user should know what each layer affects and why.

- adjustment layers
- pixel/fill/text/source layers
- layer masks
- clipping masks
- groups
- opacity
- blend modes
- transforms
- layer visibility and isolation

### 5. Proof And Delivery Flow

These features make the editor trustworthy for professional output.

- histogram/scopes
- clipping overlay
- gamut overlay
- soft proofing
- preview/export parity
- export preflight
- pixel inspection

## 100-Point UI/UX And Capability Fix Backlog

This backlog is the concrete audit list for the current cluttered editor state.
Each item must either be fixed, proven by automated tests, deliberately marked
experimental, or removed from the primary workflow.

1. [x] Remove any blocking center preview-render overlay from the image canvas.
2. [x] Show preview rendering as a compact nonblocking status badge.
3. [x] Surface preview render failures with a retry path instead of a modal-feeling overlay.
4. [x] Keep the main image visible unless the user explicitly enters a scopes-only mode.
5. [x] Make scopes view mode an explicit Image/Large choice, not a toggle with changing meaning.
6. [x] Prevent diagnostic overlays from enabling multiple competing overlays at once.
7. [x] Keep diagnostic overlay choices collapsed until requested.
8. [x] Give every overlay a clear active state and one-click disable path.
9. [x] Ensure overlays never obscure primary canvas controls by default.
10. [x] Add deterministic tests for overlay disabled state leaving image pixels unchanged.
11. [x] Move crop controls into a Crop inspector tab instead of Detail.
12. [x] Separate crop box reset from transform reset.
13. [x] Make crop guide mode a single explicit control with all supported modes.
14. [x] Verify crop guide overlays stay visually quiet at default opacity.
15. [x] Prove crop normalized bounds map exactly to exported pixel bounds.
16. [x] Prove crop aspect constraints remain stable while dragging.
17. [x] Prove horizon straightening computes the expected angle from gesture geometry.
18. [x] Ensure crop handles have generous hit targets without visually bloating the UI.
19. [x] Ensure crop active state is unmistakable on canvas and in inspector.
20. [x] Add preview/export parity tests for crop and transform.
21. [x] Add an adjustment search/filter so users can find controls without scrolling.
22. [x] Keep adjustment groups hidden when they do not match the current filter.
23. [x] Make filtered adjustment state testable with a deterministic signature.
24. [x] Keep the default adjustment panel ordered by photographer expectation.
25. [x] Add per-section before/after controls for tone, color, detail, raw, and advanced color.
26. [x] Prove before/after toggles do not mutate recipe values.
27. [x] Mark changed controls without adding visual clutter.
28. [x] Ensure group reset touches only the intended adjustment group.
29. [x] Prove typed numeric input and dragged slider input produce identical state.
30. [x] Add math tests for every signed slider's formula and render direction.
31. [x] Replace one-letter HSL channel labels with readable compact labels.
32. [x] Add tooltips to HSL channel buttons.
33. [x] Prove HSL channel buttons expose stable labels, names, and tooltips.
34. [x] Verify color-only tools preserve neutral ramps when they should.
35. [x] Verify tone-only tools preserve chroma relationships within tolerance.
36. [x] Add identity/default recipe tests for every global adjustment group.
37. [x] Add monotonicity and finite-output tests for tone curve operations.
38. [x] Prove settled preview and export match for global edits within named tolerance.
39. [x] Keep raw-only controls hidden or disabled when RAW state is unavailable.
    - Status: StudioWorkspace now exposes a deterministic RAW availability signature and the active smoke path proves JPEG assets hide the RAW inspector and omit RAW from filter results. The legacy EditView batch selector also filters RAW out of available/active copy-paste groups for non-RAW assets. EditorPreviewController coverage proves JPEG assets never report RAW controls and RAW extensions do when the platform decoder supports them.
40. [x] Explain disabled raw/model/profile controls with concise tooltips or status.
    - Status: StudioWorkspace now returns deterministic disabled-reason strings for RAW controls, smart-selection/inpaint model paths, and camera-input/soft-proof profile controls. A RAW filter query on JPEG assets now shows a specific RAW-source requirement instead of a generic empty result, and desktop smoke coverage proves the reasons are present.
41. [x] Make mask creation mode clear: source, operation, and affected layer.
    - Status: The active mask toolbar now reports source kind, combine operation, inversion state, and the adjustment-layer target in one status string. The desktop smoke test proves the same fields through a deterministic mask creation context signature after creating an intersecting linear source.
42. [x] Prevent mask toolbars from stacking over the image in confusing ways.
    - Status: Active canvas tools use `activeCanvasToolChromeReduced` so brush, crop, and retouch HUDs own the canvas instead of stacking with generic zoom/action chrome or the mask stack toolbar. The desktop smoke test already proves brush and retouch modes hide the generic edit toolbars and mask-stack strip while their HUDs are active.
43. [x] Show exactly one active mask editing context at a time.
    - Status: StudioWorkspace now exposes an active mask-context signature that counts mask-creation, brush, and retouch contexts. Desktop smoke coverage proves brush mode reports only the brush context and retouch mode reports only the retouch context while generic mask chrome is suppressed.
44. [x] Make mask stack rows readable and non-overlapping at supported widths.
    - Status: Mask stack chips now expose a layout-fit signature covering fixed row height, thumbnail width, and label column size. The desktop smoke test proves visible mask stack rows fit within the compact chip height and report `fits:true`.
45. [x] Prove mask add/subtract/intersect match set algebra on synthetic masks.
    - Status: `editor_preview_math_test` now renders four synthetic pixels through two binary segmentation masks and checks exact truth-table output for add/union, subtract/difference, and intersect/intersection against selected and neutral reference pixels.
46. [x] Prove mask opacity and density use the documented blend equation.
    - Status: `editor_preview_math_test` now measures synthetic full-mask exposure in stops and asserts opacity/density resolve as `min(base, density) * opacity`, including standalone opacity, standalone density, and combined cap-before-opacity cases.
47. [x] Ensure brush size, flow, hardness, spacing, and smoothing value text matches render behavior.
    - Status: Brush HUD value text now comes from one QML formatter for size, flow, hardness, spacing, and smoothing, using the same controller properties the renderer and brush rasterizer consume. Desktop smoke coverage sets live brush values, checks the exact HUD signature, checks the visible status text, and verifies the cursor label uses the same size/hardness text.
48. [x] Group a brush stroke into one undo step.
    - Status: `editor_preview_controller_test` now commits multiple brush samples inside one active stroke, proves the completed stroke creates one undo entry, proves one undo removes all samples from that stroke without deleting the mask, and proves one redo restores the full grouped stroke.
49. [x] Add deterministic brush-stroke tests for identical path and pressure samples.
    - Status: `editor_preview_controller_test` now replays the same three-point pressure/tilt brush path twice with fixed size, flow, spacing, smoothing, and auto-mask state, then asserts exact equality for committed brush samples and rasterized segmentation bytes.
50. [x] Ensure switching tools clears armed transient mask states safely.
    - Status: StudioWorkspace now routes tool changes through a transient-state cleanup helper that ends active brush strokes, deselects active mask context when leaving mask tools, and clears crop/compare state. Desktop smoke coverage starts an active brush stroke, switches to Adjust, and proves the resulting state is `activeMask:false|stroke:false|crop:false|compare:false` without deleting the mask.
51. [x] Make retouch a coherent workflow rather than hidden mask settings.
    - Status: StudioWorkspace now exposes a deterministic retouch workflow signature with mode label, required next action, source state, apply availability, regenerate availability, and before/after state. Desktop smoke coverage proves the HUD owns the canvas and reports the workflow state for active retouch.
52. [x] Separate clone, heal, patch, and remove/inpaint modes visually and in state.
    - Status: The retouch HUD has distinct mode buttons and active state for Heal, Patch, Remove, and Clone Stamp. Desktop smoke coverage starts each mode directly and proves the workflow signature changes to the expected mode, label, and action state.
53. [x] Expose clone source picking and current source status clearly.
    - Status: Clone Stamp reports `Pick source` until a clone source is set, then reports `Source set`; the viewport clone-source pin exposes the same source status. Desktop smoke coverage proves both states through HUD text, pin state, and the retouch workflow signature.
54. [x] Add apply/regenerate/cancel states for generated retouch operations.
    - Status: The retouch workflow signature now exposes apply and regenerate booleans. Desktop smoke coverage proves Heal/Patch/Remove allow apply and regenerate while Clone requires source selection and disables regenerate; the existing cancel path proves Escape returns retouch to neutral Adjust state.
55. [x] Prove clone samples from expected source coordinates.
    - Status: `editor_preview_math_test` already proves clone stamp copies the source color into the painted destination and separately proves clone rotation samples from the expected rotated source patch around the picked source point.
56. [x] Prove heal/patch/remove preserve finite output and alpha.
    - Status: `editor_preview_math_test` now runs remove, heal, and patch retouch masks over synthetic alpha-bearing pixels and asserts every output channel is finite while alpha remains unchanged.
57. [x] Prove inactive retouch operations do not affect preview or export.
    - Status: `editor_preview_math_test` now applies a disabled retouch mask over synthetic pixels and asserts exact no-op output. Existing preview/export parity coverage uses the same render state path for visual output.
58. [x] Add before/after coverage for retouch without mutating the recipe.
    - Status: `editor_preview_controller_test` already proves retouch before view strips retouch/inpaint masks from the preview render state and does not mutate the committed retouch mask.
59. [x] Make failed inpaint/model paths recoverable without blocking the canvas.
    - Status: StudioWorkspace now reports generated retouch as `model-ready` when AI inpaint is available and `local-fallback` when it is not, with a recoverability signature that always keeps `canvasBlocking:false` and preserves Apply/Regenerate availability. Desktop smoke coverage proves the Remove workflow exposes that recoverable state instead of blocking the canvas.
60. [x] Mark unsupported retouch tools experimental or hide them from primary UI.
    - Status: Retouch mode capability state now marks Skin Cleanup and Frequency Smoothing as `experimental-hidden`, hides their HUD buttons from the primary retouch controls, and exposes a deterministic primary-tool signature. Desktop smoke coverage proves the primary HUD keeps Remove, Heal, Patch, Clone, Dust, and Red Eye available while hiding the experimental modes.
61. [x] Make layer rows tall enough for all controls with no overlap.
    - Status: Studio layer rows expose a deterministic `layoutSignature` and `layoutFits` property and size from content height. Desktop smoke coverage proves the row fits its thumbnail, labels, opacity slider, blend dropdown, state controls, and structure actions without old +/- opacity buttons.
62. [x] Keep layer action buttons icon-first and tooltip-backed.
    - Status: Layer mask, clipping, transform, visibility, isolate, lock, move, duplicate, and delete actions are square icon buttons with deterministic accessible names derived from tooltips. Desktop smoke coverage proves those names and enabled states.
63. [x] Make layer opacity a slider plus exact percent readout.
    - Status: Layer rows now use `photoLayerOpacitySlider` with `photoLayerOpacityLabel` showing exact integer percent. Desktop smoke coverage proves the slider/readout exist and the old increment/decrement opacity buttons are absent.
64. [x] Use a dropdown for layer blend modes instead of cycling hidden options.
    - Status: Layer blend mode is a `ToneComboBox` backed by the explicit photo-layer blend mode list. Desktop smoke coverage proves the dropdown exists in each row instead of a hidden cycling button.
65. [x] Show layer masks, clipping masks, visibility, lock, solo, and transform states clearly.
    - Status: Layer rows expose mask badges, clipping-mask state, visibility, isolate, lock, and transform controls plus audit/dependency labels. Controller and desktop smoke coverage prove the metadata and controls reflect current layer state.
66. [x] Add layer visibility-state save/apply/delete controls with disabled states.
    - Status: Studio exposes Save/Apply/Delete visibility-state controls with disabled Apply/Delete until a state exists. Desktop smoke coverage saves, applies, deletes, and verifies the control states and summary text.
67. [x] Prove layer visibility and isolation affect preview/export consistently.
    - Status: `editor_preview_math_test` proves hidden layers do not affect rendered pixels, while `editor_preview_controller_test` now proves layer isolation constrains the interactive render state to exactly the isolated layer. The same render state feeds preview/export layer composition.
68. [x] Prove layer opacity equals the documented compositing formula.
    - Status: `editor_preview_math_test` now composites a synthetic fill layer and asserts exact output for `effective_alpha = source_alpha * layer_opacity` and channel interpolation `base + (blend - base) * effective_alpha`.
69. [x] Prove blend modes match formula references on synthetic pixels.
    - Status: `editor_preview_math_test` now checks exact synthetic-pixel formulas for normal, multiply, screen, overlay, soft-light, hard-light, difference, lighten, darken, and linear-light layer blend modes, plus bounded output for the HSL-family modes.
70. [x] Prove clipping masks affect only the intended target layer.
    - Status: `editor_preview_math_test` already proves a clipped adjustment layer only affects pixels covered by the layer below and leaves uncovered pixels unchanged.
71. [x] Make photo-layer direct transform handles discoverable from the layer row.
    - Status: Studio rows expose a transform icon that selects the layer and activates `LayerTransformOverlay`; desktop smoke coverage proves move, rotate, and scale handles plus the transform label become visible from the layer row.
72. [x] Prove layer transform handle movement maps viewport deltas to image coordinates.
    - Status: `LayerTransformOverlay` exposes testable move/scale/rotate helpers and desktop smoke coverage invokes `moveByViewportDelta`, then asserts the layer x/y metadata equals the viewport delta divided by the overlay scale.
73. [x] Add reorder, duplicate, rename, and delete affordances or hide unfinished layer operations.
    - Status: Layer rows expose move up/down, duplicate, editable name, and delete controls. Desktop smoke coverage proves duplicate/delete dispatch through the controller and now proves editing the layer name field updates controller state.
74. [x] Ensure locked layers block mutation but still allow selection and inspection.
    - Status: Locked rows disable rename, duplicate, and delete mutation while preserving visible row state and active transform selection. Controller coverage rejects rename/transform/delete on locked layers, and desktop smoke coverage proves locked rows remain selected/inspectable.
75. [x] Keep scopes controls grouped as display settings, not recipe-changing edits.
76. [x] Prove RGB/luma/density/trace toggles do not mutate the edit recipe.
77. [x] Prove histogram bins sum to the sampled pixel count.
78. [x] Prove scopes derive from rendered preview state rather than stale input.
79. [x] Add pixel inspector sampling only while explicitly sampling.
80. [x] Clear pixel inspector state on release, cancel, or tool exit.
81. [x] Prove pixel inspector coordinates map to the same preview/source pixel.
82. [x] Add output color-space and proofing status where scopes/gamut need it.
83. [x] Prove gamut and clipping thresholds match output profile/headroom rules.
84. [x] Keep large scopes from stealing context panel space unless in scopes-only mode.
85. [x] Collapse complex toolbar tools behind a single Diagnostics control.
86. [x] Add focus edit mode for hiding chrome without entering fullscreen state.
87. [x] Prove focus edit hides context, inspector, and filmstrip while preserving edit state.
88. [x] Add Esc cancel behavior for active tools and transient panels.
89. [x] Prove cancel returns to Adjust without corrupting mask/layer/compare state.
90. [x] Ensure toolbar buttons never overlap at supported viewport widths.
91. [x] Keep all compact-mode labels and buttons fitting inside their controls.
92. [x] Add responsive panel density modes with deterministic width signatures.
93. [x] Ensure command palette includes every primary tool and workspace command.
94. [x] Add keyboard-only paths for core edit, crop, mask, compare, and export workflows.
95. [x] Add accessible names or labels for icon-only controls.
96. [x] Keep activity indicators clear of the filmstrip, footer, and inspector controls.
97. [x] Make long-running export/import/model jobs cancellable where supported.
98. [x] Surface failed jobs as compact retryable statuses.
99. [x] Prove retrying a failed job requeues it and clears failure chrome.
100. [x] Add capability registry and smoke coverage so every exposed capability is counted and test-gated.

## Audit Matrix

Fill this table one tool at a time. Do not mark a tool complete until the problems column is empty or deliberately accepted.

| Tool | Findable | Active State | Direct Control | Fast | Accurate | Undo/Reset | Tests | Problems |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Crop | Yes: Crop inspector tab and toolbar command | Canvas border, HUD submode, inspector tab | Crop box, aspect menu, guide controls, reset crop | Draft preview, quiet guides | Exact crop/export bounds | Separate crop/transform reset, undo path | desktop smoke, controller, export job, render parity | None open |
| Transform | Yes: Crop/Transform tab and layer row Xform | Active handles and transform label | Direct handles, numeric sliders, reset transform | Draft transform preview | Viewport/image coordinate tests | Reset transform, locked-state rejection | desktop smoke, controller, math | None open |
| Exposure | Yes: Light group/filter/search | Modified marker and signed slider fill | Slider and typed value field | Draft preview | Signed formula and monotonic tests | Group reset, before/after | desktop smoke, math, parity | None open |
| Temperature/Tint | Yes: White Balance group and sampler | Color track and signed/Kelvin values | Sliders, typed values, sample mode | Draft preview | WB math and histogram tests | Group reset, before/after | desktop smoke, controller, math | None open |
| Curves | Yes: Tone Curve group | Channel/mode badge, selected point state | Point editor, preset preview, delete/reset | Draft preview | Monotonicity and finite-output tests | Reset/delete without recipe mutation | desktop smoke, math | None open |
| Presets | Yes: preset controls and command surfaces | Preview state separate from applied state | Preview/apply/save/import paths | Draft preview | Preview does not mutate recipe | Reset/apply scoped to declared groups | controller, desktop smoke | None open |
| Brush Mask | Yes: Mask/Brush HUD | Brush HUD owns canvas, cursor label | Size, flow, hardness, spacing, smoothing, add/erase | Live brush preview | Deterministic path/pressure and mask equation | One undo per stroke | desktop smoke, controller, math | None open |
| Linear Mask | Yes: Mask source toolbar | Mask source/operation/target status | Linear source, add/subtract/intersect | Draft preview | Boolean set algebra | Tool-switch cleanup | desktop smoke, math | None open |
| Radial Mask | Yes: Mask source toolbar | Overlay and active mask context | Radial source and source stack | Draft preview | Center/corner falloff tests | Tool-switch cleanup | desktop smoke, math | None open |
| Range Masks | Yes: capability registry and mask stack | Hidden/disabled unless coherent | Range mask controls gated by availability | Draft preview | Continuity and algebra coverage in mask math | Reset/disable state | math, controller, registry smoke | None open |
| Retouch Clone | Yes: Retouch HUD | Clone Stamp status and source pin | Source pick/move, apply, before/after | Draft preview | Source-coordinate and rotation tests | Cancel, undo/redo, before/after | desktop smoke, controller, math | None open |
| Retouch Heal | Yes: Retouch HUD | Heal mode/status, local fallback/model state | Paint/apply/regenerate/cancel | Draft preview | Finite output and alpha preservation | Apply/cancel/undo state | desktop smoke, controller, math | None open |
| Retouch Remove | Yes: Retouch HUD | Remove mode/status and fallback state | Paint/apply/regenerate/seed/fallback | Draft preview and CPU fallback | Baked inpaint patch and blend tests | Apply/cancel/undo/redo | desktop smoke, controller, math | None open |
| Layers | Yes: Layers inspector tab | Active row border, lock/solo/mask states | Opacity slider, blend combo, rename, reorder, duplicate, delete, Xform | Draft layer preview | Opacity, blend, clipping, visibility formulas | Lock, visibility states, undo-safe stack ops | desktop smoke, controller, math | None open |
| Export | Yes: Export workspace/footer command | Queue/failure/retry status | Presets, crop constraints, preflight, retry/cancel | Queued background work | Export dimensions, crop parity, profile/preflight tests | Retry/cancel recovery | export job, job queue, desktop smoke | None open |

Status: The matrix is closed against the current exposed professional workflow. Capabilities that do not meet this bar are either hidden from the primary workflow, marked experimental, or represented only in the registry until their direct workflow is coherent.

## Tool Pass Template

For each tool pass, write the result in this format.

### Tool Name

User expectation:

- What a photographer expects the tool to do.
- What should happen on click, drag, reset, undo, and export.

Current behavior:

- Where the tool lives.
- How it is activated.
- What controls appear.
- What render path it uses.

Friction:

- Findability issues.
- Confusing labels or direction.
- Slow preview paths.
- Missing direct manipulation.
- Missing proof/test coverage.

Fixes:

- UI changes.
- Controller/state changes.
- Render/performance changes.
- Tests and proof.

Acceptance:

- What must be true before this tool is considered polished.

## Specific Polish Targets

### Crop And Transform

- [x] Crop mode has a clear active border and tool header.
- [x] Aspect ratio presets are discoverable and editable.
- [x] On-canvas handles are large enough and do not fight pan/zoom.
- [x] Straighten has a direct horizon drag interaction.
- [x] Grid overlays are available but not visually noisy.
- [x] Reset crop and reset transform are separate if they affect different state.
- [x] Export crop result matches preview crop bounds exactly.

Status: Crop handles now use larger invisible hit targets while keeping compact visual knobs. Desktop smoke coverage proves a near-corner grab selects the handle and a center grab still moves the crop instead of treating the full crop as a handle.
Status: Crop mode now draws a non-interactive active border around the painted image area and the HUD header names the current crop submode. Desktop smoke coverage proves the border is visible and the header changes for Box, Transform, and Horizon.
Status: Crop guide overlays now expose all six choices as testable controls and use lower named opacity caps for grid/spiral overlays. Desktop smoke coverage proves each guide mode binds to the on-canvas overlay and the guide opacity stays below the noise threshold.

### Light And Color Controls

- [x] Control order matches photographer expectation: WB, exposure, tone, color, detail.
- [x] Every signed control has correct visual direction and value text.
- [x] Double-click reset works everywhere.
- [x] Dragging many sliders does not start competing full-resolution renders.
- [x] Changed controls are visually marked.
- [x] Panel reset only resets the current group.
- [x] Numerical input is supported where precision matters.
- [x] Preview/export parity tests cover high-risk controls.

Status: Studio `AdjustSlider` now handles reset requests at the shared wrapper level, so every adjustment slider gets double-click reset behavior by default. Desktop smoke coverage proves reset restores both a zero-default exposure slider and a nonzero-default channel mixer slider and clears their modified markers.
Status: The Studio Adjust stack now starts with White Balance, Light/Exposure, Tone Curve, Color, then Presence, with RAW moved below the primary creative controls. Desktop smoke coverage proves the visible primary group order.
Status: Shared signed value text now includes explicit units (`+40%`, `-40%`, `+0.85 EV`) and centered signed sliders fill away from neutral in the correct direction. Desktop smoke coverage proves positive values fill right, negative values fill left, and tint keeps directional track colors.
Status: Precision value fields are now testable on shared sliders. Desktop smoke coverage proves typed exposure input accepts EV text (`+1.37 EV`) and white-balance input accepts Kelvin text (`6500K`) through the same slider value-field path users edit.
Status: Changed adjustment sliders expose a visible modified marker tied to the shared `modified` state. Desktop smoke coverage proves the exposure marker appears after a precise edit and clears when the slider resets to default.
Status: Rapid adjustment changes now have explicit controller coverage. The preview scheduler coalesces a burst across Light, Color, Presence, and White Balance controls into draft interactive work, keeps full-resolution rendering deferred, and proves the pending full render uses the latest adjustment state rather than stale intermediate slider values.
Status: Render parity coverage now names high-risk Light and Color recipes directly: white balance/tone, tone curve, HSL/selective color, RGB channel mixer, black-and-white mixer, color replacement/harmony, and presence/grading. Each recipe must visibly affect the source and match between preview and full-resolution export rendering within tolerance.

### Tone Curve

- [x] Curve points are easy to select, drag, delete, and reset.
- [x] Channel mode is obvious.
- [x] Histogram backdrop does not obscure point editing.
- [x] Curve presets are previewable and reversible.
- [x] Point curve and parametric curve interaction is understandable.
- [x] Tests cover neutral preservation, per-channel behavior, and endpoint behavior.

Status: Tone Curve now exposes the shared editor as a testable Studio control with visible channel/mode and point-count badges, explicit selected-point delete, and active-curve reset. Desktop smoke coverage proves channel mode is visible, selected interior points can be deleted through the control path, and reset restores the active curve.
Status: Tone Curve histogram opacity is now capped through named editor properties instead of hidden canvas literals. Desktop smoke coverage proves primary and RGB overlay opacity stays below the point-editing noise threshold.
Status: Curve presets now expose non-mutating preview point data from the controller, and the shared editor can preview a preset curve on hover without applying it. Controller and desktop smoke coverage prove previewing and clearing the preview leaves the active recipe unchanged.
Status: The controller now exposes the active curve mode (`Neutral`, `Parametric`, or `Point`) and the shared editor includes it in the channel badge. Desktop smoke and controller coverage prove RGB sliders report Parametric mode, drawing/applying a point curve switches to Point mode, and RGB parametric controls clear when point curves take over.
Status: Curve math coverage now includes identity point-curve neutral preservation, endpoint black/white point behavior, red-channel isolation, and luma curve chroma-ratio preservation.

### Presets

- [x] Presets show what settings they change.
- [x] Preset intensity behavior is clear and predictable.
- [x] Applying a preset can target all settings or selected groups.
- [x] Saved presets can be renamed, deleted, and organized.
- [x] Preset hover/preview does not permanently mutate the recipe.
- [x] Preset application is recorded clearly in history.

Status: Presets now expose changed-control chips through `presetChangedControls(...)`, scoped by the active adjustment groups and current intensity. Controller coverage proves Landscape Pop reports Texture, Clarity, and Vibrance before apply, and reports no changed controls at zero intensity.
Status: Preset application now has one shared preview/apply recipe path for intensity and group scoping. Batch application in Edit View uses `presetPreviewRecipeJson(...)`, so the recipe applied to selected assets matches the user-selected intensity and active groups instead of the raw preset JSON.
Status: Presets can be applied to all declared settings or selected groups. Controller coverage proves detail-only application updates Texture/Dehaze while leaving excluded color controls untouched, and RAW-only application restores RAW decode controls without touching tone controls.
Status: User preset management now supports rename, delete, custom pack save, and pack move from the controller, with the Edit View save row exposing both preset name and pack fields. Controller coverage proves rename preserves favorites, rejects built-in and duplicate destinations, moves presets between packs/categories, keeps search working by pack name, and deletes the organized preset afterward.
Status: Preset preview JSON is non-mutating. Controller coverage proves previewing Landscape Pop at 50% intensity for the detail group produces the expected Texture/Clarity recipe while leaving the current controller state unchanged.
Status: Applying a preset records a useful undo/status event. Controller coverage proves applying Landscape Pop creates undo history and reports `Applied preset 'Landscape Pop'` in status.

### Mask Stack

- [x] Creating a mask immediately shows the mask in the stack and on canvas.
- [x] Active mask is unmistakable.
- [x] Add, subtract, intersect, and invert are clear controls.
- [x] Overlay mode is easy to switch: red overlay, matte, marching ants, edge.
- [x] Mask enable/disable and solo are available.
- [x] Mask thumbnails reflect the real matte.
- [x] Mask settings are grouped separately from local color adjustments.
- [x] Selection refine controls explain their effect visually.
- [x] Reusable mask outputs are visible as dependencies.

Status: The Studio mask toolbar now shows a concise active-mask state summary with source type, combine operation, and normal/inverted state. Desktop smoke coverage proves creating a Linear mask with Intersect immediately shows `Linear source / Intersect / Normal`, and toggling inversion updates the same status to `Inverted`.
Status: Studio now shows a compact `MASK STACK` strip immediately after mask creation, with the active mask set name and source count, and the edit viewport mounts a mask canvas overlay instead of the previous generic tint. Desktop smoke coverage proves creating a Linear mask immediately shows the stack chip and an on-canvas overlay while hiding the placeholder tint.
Status: The detailed Mask inspector now uses the same normalized source/operation/state language in the active mask card and source rows. This makes source rows distinguish Normal/Inverted, Off, Solo, and Locked without relying only on borders or button state.
Status: Add, Subtract, Intersect, Exclude, Replace, Union, and Invert controls now have testable object names and explicit tooltips in Mask Properties. Existing smoke coverage already proves the shared creation toolbar applies Intersect and Subtract to newly created linear and brush masks.
Status: Overlay mode switching already exposes Overlay, Matte, On Light, On Dark, Outline, Edge, and Affected buttons in Mask inspector, with controller coverage proving preview sources change across matte/outline/edge modes.
Status: Enable/disable and solo are available per mask source and at the active-mask property level; source-row summaries now reflect Off and Solo state directly.
Status: Mask sets and individual sources now expose compact `thumbnailSource` PNG mattes generated from the same local-mask evaluation path as active previews, and the Mask Sets list renders those thumbnails instead of only colored dots. Controller coverage proves set/source thumbnails are real PNG data URLs, update when a smart-selection matte changes, and exist for grouped refine brush sources.
Status: Reusable mask outputs and source dependencies are now visible in the Mask Sets model and UI. Sources expose `dependencyLabel` for reusable outputs and parent dependencies, the inspector lists reusable outputs, and controller coverage proves a dependent radial source reports `Depends on Reusable Base` while the base source reports `Reusable output: base-alpha`.
Status: Mask inspector sections are explicitly split into Mask Properties, Selection, Edge, Adjust, Shape, Brush, AI Refine, and Range Refine, with stable object names on the core sections. The QML build proves the grouped inspector compiles, and the existing controller coverage proves selection/edge/range controls write the intended mask state separately from local color adjustments.
Status: Selection refine now includes a compact visual effect meter above the Edge sliders. It reports neutral vs active refine state, shows Normal/Inverted state, and draws per-effect bars for Expand/Contract, Feather, Smooth, Fine Edge, Edge Contrast, and Spill cleanup; focused build coverage proves the refined UI compiles, while controller/math coverage proves those controls affect matte evaluation.

### Brush Masks

- [x] Brush cursor shows true size, hardness, and mode.
- [x] Brush HUD shows size, flow, feather/hardness, erase/add/subtract.
- [x] Keyboard shortcuts adjust size and feather without leaving the canvas.
- [x] Tablet pressure is visible and testable.
- [x] Brush strokes have immediate overlay feedback.
- [x] Undo removes the last stroke, not the whole mask unless expected.
- [x] Stroke smoothing does not lag behind the cursor.

Status: The Studio edit viewport now exposes a real brush cursor ring with testable diameter, hardness diameter, active Add/Erase mode, and live pressure readout. The HUD reports Size, Flow, Hardness, and Pressure from the same controller state, and desktop smoke coverage proves the HUD, cursor, mode, and pressure text update while painting.
Status: Brush shortcuts are now discoverable/testable on the Studio workspace for Add, Erase, size down/up, and hardness down/up. Desktop smoke coverage proves the shortcut objects exist beside the HUD controls, and the adjustment functions still update the controller values without leaving the canvas.
Status: Brush strokes now commit as explicit undo transactions. The controller captures the exact pre-stroke edit state at stroke start, records the active mask index in in-memory history state, and pushes that state only when the stroke changes the mask. Controller coverage proves undo removes the last completed stroke samples without removing the active brush mask.
Status: Immediate brush feedback is covered by the live stroke path: active brush strokes expose interpolated live samples before commit, build a live raster preview source, and clear the transient live buffer after commit. Existing controller coverage proves long drags create continuous mattes and separated strokes do not bridge through gaps.
Status: Brush smoothing now dampens intermediate stroke samples without moving the live endpoint away from the cursor. Controller coverage proves a fully smoothed stroke still exposes and commits the exact current cursor point while keeping earlier samples damped.

### Retouch

- [x] Retouch tools have distinct modes and names, not buried generic mask state.
- [x] Clone source pin is visible and movable.
- [x] Heal/clone/remove communicate whether a source is required.
- [x] Retouch strokes are listed and individually removable.
- [x] Retouch before/after toggle isolates retouch only.
- [x] Dust and red-eye tools expose a review workflow before committing.
- [x] Local algorithm fallback is clear when advanced inpaint is unavailable.

Status: The Studio retouch HUD now includes a mode-specific status label (`Remove`, `Heal`, `Patch`, `Clone Stamp`, `Dust Cleanup`, `Red Eye`, `Skin Cleanup`, or `Frequency Smoothing`) plus the current requirement and Before/After state. Desktop smoke coverage proves Heal reports `Paint area`, Remove reports `Remove`, and Clone Stamp reports `Pick source` before a source is selected.
Status: Clone Stamp now has a primary Studio viewport source pin once a source is picked. The pin reports `Source set`, displays on the canvas independently of the older mask overlay path, and exposes a move handler; desktop smoke coverage proves moving the pin updates the active clone source coordinates in the controller.
Status: Retouch operations are listed separately from selection masks in the Retouch inspector stack with Select, Apply, Cancel, Rename, Off/On, Up, Down, and Delete actions. Existing controller coverage proves retouch entries can be renamed, disabled, reordered, deleted, canceled while pending, and applied without collapsing the whole retouch stack into generic mask state.
Status: Retouch before/after is available in both the Studio HUD and Retouch inspector. Controller coverage proves Before disables retouch masks only for the preview render request without mutating the committed masks, and After restores retouch masks in preview state.
Status: Dust removal, reusable sensor dust maps, and red-eye removal now stage detected retouch masks for review instead of committing immediately. Controller coverage proves each generated mask exposes a preview matte and remains uncommitted until Apply, and proves staged dust/red-eye edits can be discarded before commit.
Status: Local inpaint fallback is explicit in the retouch stack audit text as `Local fallback`, and controller coverage proves the CPU fallback writes deterministic patch data, model name, prompt, seed, blend amount, candidate count, source mask, and diagnostics when advanced inpaint is unavailable.

### Layers

- [x] Layer stack has thumbnails, visibility, opacity, blend mode, and active state.
- [x] Layer masks are visible as masks, not hidden layer properties.
- [x] Clipping masks show dependency on the layer below.
- [x] Blend mode changes preview instantly at draft quality.
- [x] Layer transform handles are direct and precise.
- [x] Solo/isolate current layer is available.
- [x] Layer comps or visibility states are considered after the core stack is stable.

Status: Layer rows now expose a real thumbnail cell, visible mask badge, exact opacity percent, opacity slider, blend-mode dropdown, clipping-mask toggle, layer-mask toggle, visibility, and row isolate state. Controller coverage proves opacity, blend mode, clipping, and layer masks normalize into row metadata, stay blocked on locked layers, and carry into draft render requests. Desktop smoke coverage proves the Studio layer row reflects live opacity, blend mode, clipping, mask toggle, mask badge, and isolate/clear-isolate state.
Status: Photo layer stack changes now render through the draft preview path (`photo_layer_change`) instead of scheduling a full-resolution render; controller coverage proves layer opacity/blend changes do not arm the full render timer or pending full render request.
Status: The active photo layer now has a direct canvas transform overlay with a selectable layer target, move surface, corner scale handles, rotate handle, and live X/Y/scale/rotation readout. Desktop smoke coverage proves the Studio layer row exposes transform targeting, the overlay/handles are visible for an editable text layer, and a viewport move updates exact layer transform metadata through `setPhotoLayerTransform`.
Status: Layer visibility states now exist as a lightweight layer-comp foundation: the controller can save the current enabled/solo state for each layer by stable layer id, restore it later without touching locked layers, and delete saved states. Studio exposes Save/Apply/Delete visibility-state controls in the layer panel, and controller plus desktop smoke coverage prove saved states summarize visible layers, restore visibility/isolation, and return the stack to normal editing.
Status: Studio `MiniButton` now resolves an accessible name for icon-only buttons from explicit names, visible text, tooltip text, or icon fallback. Desktop smoke coverage proves representative icon-only layer row controls expose deterministic accessible names from their tooltips.
Status: Studio layer rows now expose structure operations for lock, move up/down, duplicate, and delete in a second compact row instead of hiding controller-supported layer operations. Desktop smoke coverage proves locking disables destructive row operations and duplicate/delete dispatch through the visible layer row controls while keeping the stack valid.

### Scopes And Proof

- [x] Histogram/scopes update without blocking the UI thread.
- [x] Clipping and gamut overlays clearly indicate what target they refer to.
- [x] Soft proof profile is visible near export settings.
- [x] Pixel inspector can read before/after values.
- [x] Preview/export diff exists for proofing difficult recipes.
- [x] Export preflight catches profile, gamut, clipping, size, and missing-file issues.

Status: Histogram and scope computation now exposes explicit async activity state while work is running through the existing `QFutureWatcher` path. Controller coverage proves histogram/scope updates queue without blocking the caller, publish histogram/waveform/vectorscope data after completion, and clear the active state when idle. The Studio scopes inspector also surfaces the background update state, with desktop smoke coverage proving the status label is present.
Status: Preview rendering is now represented by compact badge chrome instead of center-blocking canvas chrome. Studio exposes `previewRenderPresentationSignature` and `previewRenderChromeSignature`, and desktop smoke coverage proves updating previews are nonblocking stale badges, source-unavailable failures are retryable badges, and no preview state asks for centered blocking UI. The preview controller also exposes `retryPreviewRender`; controller coverage proves a missing source settles as a nonblocking failure and retry reattempts without opening a blocking state.
Status: Scope and diagnostic overlay entry points are explicit and collapsed. Desktop smoke coverage proves Image/Large scope view modes are deterministic, the photo remains visible in Image mode, Large mode is the only scopes-only state, and returning from Large restores the image viewport. The same smoke path proves diagnostics start closed, analysis overlays are mutually exclusive, overlay buttons carry active state, and the toolbar geometry stays non-overlapping at supported widths.
Status: Diagnostic overlays now have a render-state invariant. Desktop smoke coverage waits for an idle preview, toggles mutually exclusive overlays, dismisses the active overlay, and proves the overlay-disabled state returns to the default signature while leaving `currentRecipeJson`, `displayPreviewSource`, and preview processing state unchanged.
Status: Crop now owns its own inspector and canvas state instead of living under Detail. Desktop smoke coverage proves Crop activation selects the Crop tab, exposes only crop-owned groups, hides unavailable liquify controls, keeps reset crop and reset transform separate, and gives the on-canvas HUD unmistakable Box/Transform/Horizon/Done active states.
Status: Crop guide and direct-manipulation behavior is covered end to end. Desktop smoke coverage proves guide modes live in one selector, all supported guide names bind to the crop overlay, guide opacity stays below the visual-noise threshold, handles have at least 28 px hit targets while visual handles stay compact, and horizon dragging computes the expected straighten angle from normalized endpoints.
Status: Crop geometry and export parity are numerically gated. Controller coverage proves crop reset preserves transform state, transform reset preserves explicit crop geometry, aspect presets and custom ratios parse and round-trip correctly, and crop constraints remain stable. Export coverage proves normalized crop bounds map to the exact expected pixel rectangle and that the exported crop matches the preview crop pixels within the named one-channel tolerance.
Status: Adjustment filtering and section-level compare controls are covered by smoke tests. The adjust inspector exposes a searchable filter field, deterministic `editAdjustmentFilterSignature`, exact default group order, hidden nonmatching groups, and compact before/after buttons per section. Desktop smoke coverage proves before/after group toggles mutate only compare-group state, not adjustment values.
Status: Adjustment value input and reset behavior is now test-gated. Desktop smoke coverage proves changed sliders expose modified markers and filled rails without layout churn, typed exposure/Kelvin/degree/percent inputs parse to displayed units, and typed exposure input produces the same `currentRecipeJson` as the slider drag signal. Controller coverage proves group reset is scoped to the requested white balance, light, and color groups while preserving unrelated controls.
Status: Global signed slider render direction is now mathematically gated. `editor_preview_math_test` contains a 24-control non-spatial registry for temperature, tint, exposure, tone, levels gamma, zone mapping, parametric curve, vibrance, and saturation, plus a 3-control spatial registry for texture, clarity, and dehaze. Each case compares neutral, positive, and negative rendered metrics against the intended direction, and the test also now isolates extended-headroom chromatic highlight behavior and color-range refinement with deterministic fixtures.
Status: HSL channel controls now use readable compact labels and tooltip-backed names instead of one-letter buttons. Desktop smoke coverage proves the Red and Magenta channel signatures expose stable channel keys, visible labels, and nonempty tooltips, with the same pattern applied across the channel row.
Status: Hue/chroma-selective color tools now have a neutral-ramp preservation gate. `color_tool_invariant_test` contains a 15-case registry covering vibrance/saturation, legacy and per-channel HSL/HSV/LCH/OKLab mixers, selective color, black-and-white mixer, color replacement, skin protection, color harmony, and identity LUT stack behavior. Each case proves gray ramps keep alpha, finite output, neutral-axis equality, and luminance within tolerance. This caught and fixed a pipeline bug where active mixer state could darken neutral highlights through OKLab gamut fitting even when no hue/chroma edit should apply.
Status: Tone-only chroma preservation is now numerically gated. `color_tool_invariant_test` contains an 8-case registry covering exposure/brightness, contrast/highlights/shadows, whites/blacks/levels endpoints, filmic/adaptive tone, levels gamma, zone mapping, parametric tone curve, and luma point curve behavior. Each case renders colored samples and proves normalized RGB channel relationships and hue stay within the named `5e-4` tolerance while alpha and finite output are preserved.
Status: Default and identity group behavior is now covered at both render and recipe levels. `color_tool_invariant_test` contains a 15-case identity registry covering raw/input transform, light, levels/advanced tone, zone mapping, white balance/color, presence/detail, optics/transform, parametric curves, identity point curves, color mixer, RGB channel mixer, disabled black-and-white, disabled advanced color, grading, and disabled LUT groups; every case must match the default render pixel-for-pixel within tolerance. `edit_recipe_state_test` also verifies the serialized default recipe exposes complete raw, global, pipeline, optics, curve, color mixer, color grading, crop/transform, and output transform group defaults without silently adding or dropping fields.
Status: Tone curve operations now have dedicated monotonicity and finite-output registries. `color_tool_invariant_test` proves monotonic neutral-ramp output for parametric curves, master point curves, luma point curves, red/green/blue channel curves, and stacked master+luma curves. A separate finite-output registry covers extreme parametric settings, clamped/out-of-range master points, unsorted luma points, and deliberately nonmonotonic RGB channel curves, requiring finite alpha-preserving SDR output without assuming user-drawn nonmonotonic curves should be reshaped.
Status: Settled preview/export parity for global edits now has named tolerance gates. `render_parity_test` uses `kPreviewExportParityTolerance = 2` for preview-vs-full-resolution export renders and `kEncodedExportTolerance = 1` for encoded lossless export files. The global parity gate counts seven high-risk global edit families: white balance/tone, tone curve, HSL/selective color, RGB channel mixer, black-and-white mixer, color replacement/harmony, and presence/grading. Each case must visibly affect the deterministic source and match the full-resolution export render within the named tolerance.
Status: Clipping and gamut overlays now carry explicit target labels in the edit viewport: clipping identifies preview clipping with blue shadow/red highlight semantics, and gamut identifies the active output color space. Desktop smoke coverage proves both target labels are present and readable when the overlays are enabled.
Status: The edit viewport now drives a non-render-blocking pixel inspector from normalized image coordinates. The HUD follows the cursor and reports sampled pixel coordinates, After RGB/hex, Before RGB/hex when compare/reference data is available, and RGB/luminance deltas. Controller coverage proves sampled before/after preview RGB values and deltas come from the cached rendered previews, while desktop smoke coverage proves the Studio HUD labels are present in the edit workspace.
Status: Export now has a first-class preflight result exposed by the export controller and surfaced in the Export view before queueing. It reports blocking missing source/destination errors, target format/profile, soft proof profile, estimated output size, web/profile mismatches, gamut-mapping risk, preview clipping, and RAW clipping. Export controller coverage proves profile, soft-proof, clipping, size, missing-source, and missing-destination checks are produced before an export job is queued.
Status: The Export view now shows the active soft proof profile directly in the preflight panel beside the output format/profile/size summary, so proofing context is visible before queueing the job. Export controller coverage proves the preflight result carries the soft proof profile and flags a mismatch from the export output profile.
Status: Rendered photo exports now compare the exact in-memory rendered image against the file reloaded from disk and persist a `previewExportDiff` proof with dimensions, max/mean channel delta, preset tolerance, pass/fail state, and status. Export controller coverage proves a difficult crop-bounds recipe publishes the diff, persists it in job metadata, and verifies the written lossless export stays within a one-channel tolerance of the preview render.
Status: Scope display controls are now explicitly testable presentation state. Studio smoke coverage clicks RGB, Luma, Density, Trace, Grid, and Log controls through the inspector UI, proves the scope display signature changes, and verifies `currentRecipeJson()` is byte-for-byte unchanged.
Status: Histogram computation now exposes raw per-channel bin counts alongside normalized display bins. Controller coverage proves red, green, blue, and luminance count arrays each contain 256 bins and sum exactly to `histogramSampleCount()`.
Status: Scope source data is now regression-tested against the actual adjusted preview image. Controller coverage renders an edited synthetic image, proves histogram counts match the rendered preview rather than the stale source, and checks a waveform column average against the same rendered pixels.
Status: Pixel inspector sampling is explicit transient UI state. Desktop smoke coverage proves hover alone keeps the HUD hidden, explicit sampling shows it for Adjust, Crop suppresses it, and finishing/canceling sampling clears the controller sample state.
Status: Pixel inspector coordinate mapping is now proven on gradient before/after previews. Controller coverage samples normalized coordinates, verifies the reported pixel indexes, and checks before/after RGB against the exact expected source pixels.
Status: Scopes now show compact output/proofing context in both inspector and large-scope modes. Desktop smoke coverage sets Display P3 output plus sRGB soft proof, verifies both scope status labels, and checks the deterministic proofing status signature.
Status: Clipping and gamut threshold behavior is now mathematically pinned. Controller coverage proves preview clipping counts only exact 8-bit channel endpoints 0 and 255, RAW clipping counts only 16-bit endpoint-equivalent samples, and export preflight preserves output headroom while gating gamut/clipping warnings on output profile and valid sample counts.
Status: Large scopes, diagnostics, and Focus Edit are now covered as clutter controls. Desktop smoke coverage proves large scopes only take over the image in explicit scopes-only mode, diagnostics choices stay collapsed behind one control, and Focus Edit hides context, inspector, and filmstrip without entering fullscreen or mutating the edit recipe.
Status: Escape/cancel behavior is deterministic for active tools and transient panels. Desktop smoke coverage proves cancel closes diagnostics first, exits retouch and clears active masks, leaves crop without crop editing active, and exits compare with compare mode disabled.
Status: Toolbar overlap is now checked as geometry rather than a visual assumption. The diagnostics overlay wraps into a grid at constrained widths, crop HUD buttons have stable object names, and desktop smoke coverage resizes the edit workspace through 1024, 1180, 1320, and 1600 px, proving visible toolbar buttons stay inside their parent chrome and do not overlap siblings.
Status: Compact-mode button and label fit is now a testable UI contract. `MiniButton` constrains its content row, elides long labels inside the available width, and exposes fit properties. Desktop smoke coverage enters compact mode and proves all visible `MiniButton` controls plus named compact-prone labels fit without overflowing their controls.
Status: Responsive panel density now has deterministic width signatures. The workspace exposes `responsivePanelDensitySignature(width)`, covering width, mode, interface density, small-display state, panel widths, and visibility booleans. Desktop smoke coverage proves the exact signatures for small-collapsed, compact, and pro density modes at the 980, 1119, 1180, and 1440 px breakpoints.
Status: Command palette coverage is now explicit. It exposes a required-command coverage signature, includes primary workspace navigation plus edit rail tools, and desktop smoke coverage proves required labels are present and primary edit tool commands dispatch through the shared activation path.
Status: Core workflow shortcuts are now deterministic and test-gated. Adjust, Crop, Masks, Brush, Compare, and Export actions expose platform-specific keyboard paths, the command palette shows matching hints, the shortcuts dialog lists editable workflow mappings, and desktop smoke coverage proves the exact action and editable shortcut signatures.
Status: Activity indicators now have explicit rendered height and anchored edit-workspace margins. Desktop smoke coverage proves the floating indicator rect clears the inspector panel and footer controls, checks filmstrip geometry when visible, and mathematically gates the edit bottom/right margins so status chrome stays clear of filmstrip and inspector areas.
Status: Long-running job cancellation is now policy-gated by status instead of broad job kind. The app exposes queued-drop cancellation for queued export/import/model jobs, cooperative cancellation for running export and timeline render jobs, and no cancel affordance for unsupported running import/model work. Job queue coverage proves exact cancellation policy signatures and transitions, while desktop smoke coverage proves the QML activity policy matches the backend matrix.
Status: Failed jobs now surface through compact failure activity chrome instead of occupying the canvas. Export and timeline failures are retryable, import failures remain visible without falsely advertising replay, and desktop smoke coverage proves retrying a failed export clears the failure chrome and requeues the job. Job queue coverage also proves timeline retry requeues and unsupported import retry leaves the failed record unchanged.
Status: Capability registry coverage now has a single aggregate gate in addition to the category-specific registries. Studio exposes `allCapabilityCount`, `allCapabilitySignature`, and `capabilityRegistryGateSignature`; desktop smoke coverage proves at least 179 declared capabilities are counted, representative library/export/model/acceptance capabilities appear in the aggregate signature, and duplicate capability keys are rejected.

## UX Rules For The Editor

- Do not show every control all the time.
- Do not bury the active tool state in tiny text.
- Do not make users guess whether they are editing global image state, a mask, or a layer.
- Do not let background full-resolution renders make live tools feel broken.
- Do not make powerful tools depend on hidden keyboard knowledge.
- Prefer direct manipulation on the image when the tool has geometry.
- Prefer compact, contextual inspectors over long walls of controls.
- Prefer one unified edit stack mental model over separate hidden systems.

## First Three Execution Passes

### Pass 1: Basic Edit Tool Trust

- [x] Audit all light/color slider directions, labels, ranges, reset values, and typed inputs.
- [x] Fix any control whose display value does not match render behavior.
- [x] Add tests for signed direction on high-risk color and tone controls.
- [x] Ensure slider drag schedules live preview without immediate expensive full render contention.

Status: The shared slider contract now covers signed direction, neutral/default positions, modified markers, typed raw values, typed Kelvin values, typed degree values for normalized hue controls, and typed percent values for normalized radius/position controls. Studio smoke coverage proves high-risk light/color sliders expose the expected ranges/defaults, signed adjustments fill in the correct direction from neutral, double-click reset restores declared defaults, hue fields accept displayed degree units, and normalized percent fields accept displayed percent units instead of raw 0..1 values.
- [x] Make changed controls visually obvious.

Current status as of 2026-05-24:

- Temperature display and typed Kelvin input now move in the same warm/cool direction as the render engine.
- High-risk signed controls have numeric tests for temperature, tint, highlights, and shadows.
- Rapid slider edits defer the full-resolution render until interactive preview work settles.
- The shared inspector slider now marks modified controls with accent color, a changed-state dot, and a filled value rail.
- Specialized color/detail/transform controls now use explicit slider handlers, correct identity defaults, and testable QML object names for high-risk controls.
- Channel mixer identity channels, color replacement defaults, skin-tone defaults, color-grading hue range, crop zoom, transform scale, liquify radius, and mask opacity/density/inpaint blend no longer appear modified at their neutral state.
- Per-panel reset behavior is now available from the active Studio inspector headers for White Balance, Light, Color, Presence, Detail, Lens, and RAW, with controller-level group reset behavior that preserves unrelated controls.
- Remaining audit work: broader per-tool evidence for the unchecked specific polish targets.

### Pass 2: Crop And Mask Usability

- [x] Make crop/transform active states and handles clear.
- [x] Simplify mask creation into obvious choices.
- [x] Make mask stack active/solo/disable/overlay controls clear.
- [x] Add brush HUD and reliable shortcut behavior.
- [x] Ensure mask previews update immediately at draft quality.

Current status as of 2026-05-24:

- Crop now has an explicit active HUD with Crop Box, Transform, and Done controls.
- Crop Box and Transform are separate submodes, so the transform handles are reachable instead of being hidden behind crop-box editing state.
- Desktop smoke coverage proves the QML crop HUD, crop overlay, transform overlay, and crop submode switching stay wired correctly.
- The crop inspector now exposes typed percent controls for Crop X, Crop Y, Crop W, and Crop H, plus separate Reset Crop and Reset Transform commands.
- Controller coverage proves Reset Crop preserves transform, perspective, liquify, and crop-fill state, while Reset Transform preserves explicit crop geometry and reports no-op state correctly.
- Studio now exposes the full crop aspect preset list through a combo box, plus Add and Remove controls for custom ratios backed by the controller's persisted custom preset API.
- Desktop smoke coverage proves a custom aspect ratio can be added, selected, removed, and returned to Original from the Studio crop inspector.
- Crop now has a Horizon submode with a dedicated on-canvas guide overlay that commits dragged endpoints through the same straighten-from-horizon controller path.
- Desktop smoke coverage proves Horizon mode activates a direct guide surface and that normalized guide endpoints update the rotation correction.
- Export controller coverage now uses a unique per-pixel source to prove the exported crop has the same bounds and pixels as the preview crop render.
- Mask creation now separates source choice from combine mode: Object, Brush, Lasso, Linear, and Radial are distinct from Add, Subtract, and Intersect.
- New mask sources inherit the selected combine mode, Intersect is no longer a dead control, and the creation toolbar forces the overlay preview on so the result is visible immediately.
- Desktop smoke coverage proves Intersect linear-mask creation and Subtract brush creation are wired through the QML toolbar.
- Mask preview labels now use explicit terms: Overlay, Matte, On Light, On Dark, Outline, Edge, and Affected.
- Mask source rows now expose On/Off, Solo, and Lock/Locked as direct controls instead of relying on abbreviated state letters.
- Full debug build and the focused desktop/editor suite pass with the latest crop, mask creation, mask stack, and render scheduling changes.
- Brush masking now has an on-canvas HUD with explicit Add, Erase, Size, Flow, Hardness, and Done controls.
- Studio-level brush shortcuts now route B to Add, E to Erase, and [ / ] to brush size while preserving the same QML functions used by the HUD.
- Desktop smoke coverage proves the brush HUD appears for an active brush, erase/add mode switches are reflected in the UI, and size/flow/hardness controls update the active brush.
- Mask stack edits now force a draft interactive render first and defer the settled full render behind it, so mask geometry/stack changes are not blocked by the high-quality render path.
- Controller coverage proves a new mask exposes its matte preview immediately, marks the next interactive render as draft quality, and queues the full render as pending instead of starting the full timer right away.

### Pass 3: Retouch And Layer Coherence

- [x] Split retouch modes into clear workflows.
- [x] Add retouch stroke audit/list behavior.
- [x] Make layer stack thumbnails and dependencies easier to read.
- [x] Add layer isolate/solo workflow.
- [x] Add preview/export proof for layers plus masks.

Current status as of 2026-05-24:

- Studio now exposes retouch as its own workflow with a dedicated on-canvas HUD instead of relying on hidden mask state.
- The retouch HUD gives direct entry points for Remove, Heal, Patch, Clone, Dust, Red Eye, Skin, and Smooth, plus Apply, Regenerate, Before/After, and Done controls.
- Desktop smoke coverage proves Studio can start Heal and Remove retouch modes, shows the dedicated HUD, and reflects the active retouch mode in the on-canvas controls.
- The retouch stack now exposes audit labels, mode labels, staged/applied status, stroke counts, candidate counts, and fallback markers from the controller model.
- The retouch inspector list now uses a two-line audit card with direct Select, Apply, Cancel, Rename, On/Off, Up, Down, and Delete controls.
- Controller coverage proves retouch rows expose audit metadata and still support renaming, disabling, reordering, and deleting operations.
- Layer rows now expose a thumbnail well, readable kind/status/opacity/blend metadata, dependency text for clipping/groups/masks, and direct On/Off, Solo, Lock, Up, and Down controls.
- The controller layer model now normalizes `flatIndex`, `kindLabel`, `opacityPercent`, `layerAuditLabel`, and `dependencyLabel` so QML does not have to infer stack meaning from raw layer state.
- Controller coverage proves layer rows expose readable visible/blend metadata and that the same audit label updates for hidden and solo states.
- The active Studio inspector now exposes the layer stack beside the main adjustment workflow, with Add Current, Fill, Text, isolate, clear isolation, readable row metadata, and direct row controls.
- Layer isolation now has explicit controller operations: `isolatePhotoLayer()` forces exactly one unlocked layer into solo state, and `clearPhotoLayerSolo()` returns the full stack to normal visibility.
- Desktop shell smoke coverage proves the live Studio UI exposes isolate and clear-isolate controls with the correct inactive/active state, while controller coverage proves exact solo state transitions.
- Render parity coverage now exercises a deterministic recipe with both a selection mask and a masked fill layer, proves the preview render matches the full-resolution export render within tolerance, and verifies the encoded export artifact preserves the same pixels.

## Definition Of Done

A tool is polished when a new user can find it, a serious editor can control it precisely, the preview feels responsive, the exported result matches the preview, and the implementation has enough tests or visual proof to prevent regressions.
