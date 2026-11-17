# Capability Verification Plan

This plan is the quality gate for every claimed capability in the app. A feature
is not complete because a control exists, a checklist item is checked, or a
single happy-path screenshot looks right. It is complete only when the expected
behavior is specified, the UI exposes it coherently, the underlying state is
deterministic, and automated tests prove the important invariants.

## Rule

Every capability must have:

- a user-facing workflow definition
- an explicit owner surface in the UI
- a state model with valid transitions
- a render/export contract when it affects pixels
- reset, undo, before/after, and failure behavior where applicable
- automated tests for correctness, parity, and regressions
- a short manual QA script for interaction quality that cannot be proven by math

If a capability cannot meet this bar yet, it must be marked as experimental,
hidden from the primary workflow, or removed from the professional UI until it
can be made coherent.

The guarded editor regression suite, CI merge status, hardware performance
budgets, and exact-commit native release qualification are described in
[Editor regression protection](editor-regression-protection.md). This plan remains
the acceptance owner; the executable contract enforces its named regression cases.

## Verification Levels

Use the strongest level that applies. Pixel and geometry features need
mathematical proof. Pure workflow features still need deterministic state and UI
tests.

| Level | Applies To | Required Proof |
| --- | --- | --- |
| Math invariant | color, tone, detail, masks, layers, geometry, scopes | finite output, identity preservation, monotonicity, bounded error, algebraic formula checks |
| Render parity | preview, full render, export, CPU/GPU paths | same recipe produces equivalent output within named tolerance |
| State-machine | tools, masks, layers, jobs, catalog, export | invalid transitions are impossible, undo/reset/cancel are deterministic |
| UI smoke | QML controls, HUDs, panels, keyboard paths | control is findable, active state is visible, no overlap, action reaches controller |
| Golden image | high-risk recipes and workflows | fixed corpus output stays inside perceptual tolerance |
| Performance | render, brush, import, cull, export | measured latency/throughput budgets with reproducible inputs |
| Accessibility | keyboard, focus, labels, contrast, scale | keyboard-only path, focus order, names, contrast, responsive layout |

## Capability Inventory

The audit must include every exposed and planned capability from the product
checklist, controller properties, QML controls, and command surfaces.

### Core Library And Catalog

- import from folders/cards
- duplicate detection and duplicate avoidance
- projects, albums, collections, smart collections
- ratings, pick/reject, labels, culling, compare, survey
- metadata read/write, EXIF/IPTC/XMP, captions, keywords, GPS
- offline media, relink, cache repair, catalog repair
- search, filters, saved searches, smart filters
- local files outside the catalog

Required tests:

- catalog state transitions for import/delete/reload/filter
- metadata round-trip tests
- culling hotkey and auto-advance UI smoke tests
- duplicate detection deterministic matching tests
- relink/offline recovery state tests
- large-library scroll and thumbnail performance tests

### Global Editing

- white balance, temperature, tint, sampling
- exposure, brightness, contrast, highlights, shadows, whites, blacks
- levels, zone mapping, tone curves, point curves, parametric curves
- vibrance, saturation, HSL/HSV/LCH/OKLab color mix
- selective color, channel mixer, black-and-white mixer
- color replacement, skin tone protection, harmony, split toning
- texture, clarity, dehaze, sharpening, noise reduction
- RAW controls and camera/profile controls

Required tests:

- all invariants in `docs/color-engine-math-test-plan.md`
- identity/default recipe preserves input within epsilon
- slider value maps to the documented formula
- neutral ramps remain neutral where the tool is tone-only
- settled preview and export match within tolerance
- reset restores defaults without mutating unrelated groups
- typed numeric input and drag input produce the same state

### Crop, Geometry, Lens, And Transform

- crop box, aspect presets, guides
- rotate, straighten, horizon guide, scale, skew, perspective
- guided upright, crop fill, lens distortion, vignette, chromatic aberration
- layer transform handles and direct manipulation
- liquify if it remains exposed

Required tests:

- identity transform preserves pixels and dimensions
- crop rectangle math maps normalized UI bounds to export pixels exactly
- aspect constraints remain stable during drag
- horizon gesture computes the expected angle
- inverse/round-trip transform tests for coordinate mapping
- preview/export crop parity tests
- UI smoke tests for handles, hit targets, mode switching, and reset

### Masks And Local Editing

- mask sets, sources, source dependencies, reusable outputs
- brush, erase, pressure, tilt, flow, hardness, spacing, smoothing
- linear, radial, polygon, lasso, range, alpha image masks
- smart/object/subject/sky/background/people selections
- add, subtract, intersect, invert, density, opacity, feather, refine
- overlay, matte, affected-pixels, edge, marching-ants previews

Required tests:

- mask blend equation:
  `output = lerp(base, adjusted, clamp(mask * opacity * density, 0, 1))`
- boolean operations match set algebra on synthetic masks
- feather/range masks are continuous across boundaries
- brush strokes are deterministic for the same path/pressure samples
- undo groups a stroke as one logical operation
- preview mask and settled mask agree within tolerance
- mask UI exposes exactly one active editing context at a time
- switching tools clears armed transient states safely

### Retouch And Inpainting

- clone, heal, patch, remove/inpaint
- dust, red-eye, skin cleanup, frequency smoothing
- clone source pick and transform
- regenerate, seed, candidates, apply, before/after
- persistent retouch operation list and undo

Required tests:

- clone samples from the expected source coordinates
- heal/patch/remove leave finite output and preserve alpha
- inactive/unapplied retouch operations do not change export
- apply/regenerate/undo state transitions are deterministic
- before/after toggles do not mutate the recipe
- high-resolution final pass matches preview intent within named tolerance
- UI smoke tests prove mode, apply, regenerate, before/after, and done paths

### Layers And Compositing

- adjustment, pixel/source, fill, text, and group layers
- masks, clipping masks, opacity, blend modes
- lock, visibility, solo/isolate, reorder, rename, duplicate, delete
- transform handles, layer visibility states, layer dependency labels

Required tests:

- layer stack order produces deterministic compositing
- opacity equals `lerp(backdrop, layer, opacity)` where blend mode allows it
- blend modes match formula references on synthetic pixels
- clipping masks affect only the intended target layer
- hidden/locked/solo states affect preview and export consistently
- reorder/duplicate/delete preserves valid indices and undo
- UI smoke tests prove no layer rows overlap at supported widths

### Proofing, Scopes, And Diagnostics

- histogram, waveform, parade, vectorscope
- clipping, gamut, false color, zone map, focus overlays, peaking
- pixel inspector and before/after pixel values
- soft proofing and output color space

Required tests:

- histogram bin counts sum to sample count
- scopes derive from the rendered preview state, not stale input
- RGB/luma toggles affect only display, not recipe
- overlay disabled state leaves the image unmodified
- pixel inspector coordinate maps to the same source pixel as the preview
- gamut/clipping thresholds match output profile/headroom rules
- UI tests prove overlays do not obscure controls by default

### Presets, LUTs, Batch, And Sync

- built-in and user presets
- partial presets, adaptive presets, intensity
- preset preview, save, rename, delete, import/export
- LUT import/export, LUT strength, LUT stack, color-space tagging
- copy/paste groups, selective sync, batch edit, match exposure/WB

Required tests:

- preset preview does not mutate recipe
- preset apply changes only declared groups
- preset intensity interpolates from original to target deterministically
- identity LUT preserves pixels
- LUT interpolation weights sum to one and are nonnegative
- batch operations apply exactly to selected assets and report conflicts
- copy/paste conflict preview matches final applied diff

### Export And Delivery

- export presets, formats, bit depth, resize, profile embedding
- output sharpening, watermarking, metadata controls, filenames
- collision handling, manifests, print/contact sheet workflows
- queue controls, pause/resume/cancel/retry, failure recovery

Required tests:

- exported dimensions match resize/crop contract exactly
- embedded profile and metadata settings match selected options
- color output matches preview/export parity tolerance
- filename templates and collisions are deterministic
- cancel/retry/resume leaves queue and files in a recoverable state
- validation catches impossible or unsafe export settings
- performance tests prove queue work does not block the UI

### Timeline, Carousel, And Adjacent Workspaces

- carousel canvas, image assignment, text/fill/layout tools
- timeline source panel, clips, timeline render, save/export
- shared media picker and cross-workspace asset activation

Required tests:

- workspace state does not leak into photo edit state
- timeline/carousel exports use explicit source recipes
- asset activation opens the intended workspace and selected asset
- render jobs report progress, cancel, and recover consistently
- UI smoke tests cover the main happy path or the feature is marked experimental

### AI Runtime And Model Features

- model install, versioning, acceleration detection
- CPU/GPU fallback, privacy/offline status, invalid model errors
- model warmup, cache management, batch inference
- smart selection, subject/sky/background/object masks, inpaint

Required tests:

- missing, invalid, and valid model states have deterministic UI and errors
- fallback path produces a valid procedural or disabled result
- model cache operations are reversible or recoverable
- AI-generated mask/retouch outputs are bounded and attach to recipes correctly
- batch inference jobs can be cancelled and resumed safely

## Definition Of Done For Any Capability

A capability is done only when all applicable boxes are true:

- [ ] Product behavior is written in one paragraph.
- [ ] UI entry, active state, and exit state are visible and coherent.
- [ ] Invalid states are unreachable or clearly disabled.
- [ ] Reset, undo, and before/after behavior is specified and tested.
- [ ] Preview and export behavior is specified and tested.
- [ ] Mathematical invariants exist for every pixel/geometry operation.
- [ ] Controller/unit tests prove state and math.
- [ ] UI smoke tests prove the user path.
- [ ] Golden/performance tests exist for high-risk visual or latency paths.
- [ ] The feature is removed from the primary UI if the above is not true.

## Immediate Audit Work

### Creative Tools Delivery Status — 2026-09-25

The detailed scope, numerical acceptance targets, shared gates, and evidence
requirements live in [creative tools plan](creative-tools-plan.md). Canonical
qualification status: **0/9 feature families qualified**. F1 Optical Diffusion,
F2 Imperfect Print, F3 Dragged Shutter, T1 Variable-Width Typography, B1 Dry
Ink/Graphite, B2 Light Painting, T2 Contour Typography, and T3 Material Lettering
have implementation receipts but remain in progress because corpus review and shared native, output,
agent, performance, stability, and preset gates are incomplete. B3 Photo Fragments
has native/agent alpha-preserving stamps, a versioned standalone PNG tip store, and
portable `.looksybrush` import/export. Focused offscreen tests cover source-layer
removal, transfer to a separate app-data directory, exact fresh-process PNG reload,
recipe round-trip, and undo; a full app close/reopen editing walkthrough, photographic,
backend, performance, and model-driven agent qualification remain open. Foundation
ordering evidence now includes a 48-case top-first adjustment matrix plus a locked
agent-target rejection that leaves state unchanged, and a 100-operation alternating
typography/transform history sequence with exact undo/redo recipe and pixel restoration;
native reliability and the other shared gates remain open. The production QML brush/click-away/reselect/lasso route
passed three fresh-process 100-cycle stress runs with a stub segmentation provider;
the three required native sessions and real segmentation validation remain open. T2 has native
on-canvas baseline handles, secondary contour/hole visualization, and sampled
analytic circle/ellipse deviation measurements below 0.5 source pixel; photographic
review and shared gates remain open before feature-specific acceptance is complete.
The visual qualification contract is now executable through
[the review validator](../scripts/perf/validate-creative-tools-review.py), with
[packet instructions](creative-tools-visual-review.md). Current creative corpus
coverage is now **12/12 hash-verified photos** ([corpus receipt](evidence/creative-tools/visual-review-corpus/README.md)); completed human review remains **0/216 cells**,
and family qualification remains **0/9**; validator tests do not count as visual
review evidence. A fresh focused CPU regression run passed 3/3 for render parity,
spatial effects, and paint material behavior; see the [shared receipt](evidence/creative-tools/shared-render-parity/README.md).
Production agent runs are **18/18**: A1–A6 each pass 3/3. Photographic taste
review remains open; qualified families are **0/9**, qualified presets are
**0/18**, and finished compositions are **0/6**. See the
[agent run receipts](evidence/creative-tools/agent-live-workflow/README.md).
Current receipts: [F1](evidence/creative-tools/optical-diffusion/README.md),
[F2](evidence/creative-tools/imperfect-print/README.md),
[F3](evidence/creative-tools/dragged-shutter/README.md),
[T1](evidence/creative-tools/variable-typography/README.md),
[B1](evidence/creative-tools/dry-ink-graphite/README.md),
[B2](evidence/creative-tools/light-painting/README.md),
[T2](evidence/creative-tools/contour-typography/README.md),
[T3](evidence/creative-tools/material-lettering/README.md), and
[B3](evidence/creative-tools/photo-fragments/README.md).
Do not count a vertical slice as a qualified capability.

1. Generate a capability matrix from the QML controls, controller properties,
   C++ render fields, Rust catalog/job APIs, and existing docs.
2. Mark every row as `complete`, `partial`, `placeholder`, `broken`, or
   `hidden`.
3. For every `partial` or `broken` capability, decide whether to fix, hide, or
   cut it from the primary workflow.
4. Add missing tests before adding more UI around that capability.
5. Treat failed mathematical tests as product defects, not visual preferences.

## UI/UX Fix Backlog

These fixes came from the edit-workspace audit and must be reconciled with the
capability matrix above. Each item should either become a tracked issue with
tests, be folded into a larger redesign task, or be deliberately rejected with a
reason.

### Canvas Clutter

1. Remove the centered `Rendering Preview` modal from every edit path.
2. Move render status to a non-blocking status strip outside the image.
3. Prevent the floating activity indicator from covering sliders and buttons.
4. Reserve the canvas for image and tool interaction, not status UI.
5. Consolidate the top-left zoom toolbar and top-right action toolbar.
6. Do not show global toolbars on top of active mask or brush HUDs.
7. Collapse inactive overlay buttons into one diagnostics menu.
8. Hide overlay tools unless the user enters inspect or diagnostic mode.
9. Give brush, crop, and retouch one consistent HUD position.
10. Avoid stacking two `Done` buttons for the same task.

### Rendering State

11. Show render progress only when render is actually blocking interaction.
12. Distinguish `rendering` from `preview ready but updating`.
13. Do not dim the photo during normal interactive preview renders.
14. Add a stale-preview badge instead of blocking the canvas.
15. Keep the previous preview visible while rendering the next one.
16. Disable only controls that are unsafe during render, not the whole workspace.
17. Make render status dismissible or compact after a short delay.
18. Use one render indicator across Adjust, Scopes, Detail, and Masks.
19. Avoid repeating `Rendering preview` in both canvas and inspector.
20. Add render failure and retry UI where the render status currently sits.

### Tool Model

21. Unify `editTool`, `selectedEditLayer`, and `editInspectorTab` into one tool state model.
22. Separate workspace mode from active canvas tool.
23. Make Adjust the neutral state, not a hidden fallback after `Done`.
24. Make Crop, Mask, Retouch, Compare mutually exclusive canvas modes.
25. Prevent active masks from leaving brush HUDs visible after switching inspector tabs.
26. Add clear entry and exit transitions for every modal tool.
27. Add a visible active-tool breadcrumb in one place only.
28. Disable incompatible controls instead of leaving them visually active.
29. Make tool activation reversible with Escape.
30. Preserve previous inspector tab when leaving temporary tools.

### Masking

31. Merge the mask source toolbar and brush HUD into one mask toolbar.
32. Put Add, Subtract, and Intersect beside source selection, not split across rows.
33. Rename `Object` to `Select Object` for action clarity.
34. Rename brush source vs brush editing mode to avoid ambiguity.
35. Make active mask set selection obvious on the canvas and in the layer panel.
36. Do not show mask stack cards if they overlap or compete with canvas tools.
37. Add explicit `New Mask` vs `Add Source To Current Mask`.
38. Show brush size, flow, and hardness as compact sliders or popovers, not many buttons.
39. Add a real brush cursor preview with size and hardness.
40. Make mask overlay opacity and color configurable and easy to toggle.

### Layers

41. Move photo layers out of the Adjust panel into a dedicated Layers tab or panel.
42. Reduce layer row height and visual noise.
43. Fix overlapping layer text and buttons shown in the edit screenshots.
44. Give layers drag handles and reorder affordances.
45. Add layer selection highlight that does not look like an active edit.
46. Replace `A`, `S`, `T`, and `G` badges with clearer icons.
47. Put opacity on a slider or numeric field, not `-` and `+` buttons.
48. Use a blend-mode dropdown instead of cycling by button.
49. Separate visibility, solo, lock, mask, and transform into predictable icon controls.
50. Add confirmation or undo affordance for layer visibility state delete.

### Inspector

51. Make Adjust, Scopes, Detail, and Info true tabs with consistent spacing and selected states.
52. Keep the active tab visible when scrolled.
53. Stop the status pill from floating over inspector controls.
54. Move Export out of the edit inspector footer unless in Export workspace.
55. Make Reset buttons local to the section they reset and visually subordinate.
56. Use consistent section collapse arrows and alignment.
57. Keep histogram height stable and readable.
58. Make panel scrolling independent of bottom action buttons.
59. Add search or filtering for adjustment controls.
60. Collapse advanced sections by default until core tools are stable.

### Adjustments

61. Put White Balance, Light, Color, and Presence in a standard editing order.
62. Do not show advanced tone curve before basic tone unless expanded.
63. Make value entry boxes consistent across all sliders.
64. Fix selected slider state so it does not look like a button stuck on.
65. Add double-click reset on slider labels and knobs.
66. Add per-section before/after toggles.
67. Make disabled or unavailable RAW controls hidden or explained.
68. Make color tools expose enable states consistently.
69. Avoid one-letter HSL channel buttons without tooltips or labels.
70. Add visual warnings when controls are placeholders or not wired.

### Crop / Geometry

71. Keep crop controls in Crop mode, not buried under Detail.
72. Add aspect lock and rotate/flip controls in the crop HUD.
73. Make crop guides a compact menu instead of six buttons.
74. Prevent crop HUD from covering the crop target area.
75. Add Apply and Cancel semantics for crop if changes can be staged.
76. Show current aspect ratio in the canvas HUD.
77. Make horizon straighten gesture discoverable.
78. Keep transform handles separate from photo layer transform handles.
79. Add reset buttons near the affected crop and transform controls only.
80. Hide liquify until there is a real direct-manipulation tool.

### Scopes / Diagnostics

81. Do not put full scopes in the right inspector and also offer large scopes without a clear mode switch.
82. Make Large Scopes a view mode, not a checkbox-like row.
83. Preserve image visibility when opening scopes unless user chooses scopes-only.
84. Use real icons and labels for clipping, gamut, false color, and zone map.
85. Add a single Diagnostics toggle that reveals overlay choices.
86. Prevent diagnostic overlays from making the photo unreadable by default.
87. Add overlay opacity controls.
88. Keep pixel inspector pinned near cursor only while sampling, then hide it.
89. Make before/after pixel inspector values available only in compare or sampling mode.
90. Make scope refresh status local to scopes, not global render status.

### Navigation / Layout

91. Give the left asset context panel a compact/collapse mode by default in Edit.
92. Make the filmstrip collapsible and auto-hide in focused editing.
93. Reduce persistent chrome when user is actively painting or cropping.
94. Use one primary navigation level; avoid global nav, side rail, and contextual panels fighting.
95. Persist layout choices per workspace.
96. Add a `Focus Edit` layout that hides library context, filmstrip, and inspector chrome.
97. Ensure responsive breakpoints never create overlapping HUDs.
98. Make all toolbar buttons stable width so text and icon changes do not shift layout.
99. Audit every visible command for whether it is functional, placeholder, or disabled.
100. Establish one interaction contract: select tool, adjust settings, act on canvas, finish or cancel, see result.


## Professional content tools — 2026-10-29

User-approved specifications and completion gates: [Professional content tools roadmap](photoshop-professional-tools-roadmap.md). This section owns status for the new program; masking, timeline and performance owners retain their existing acceptance scopes. Foundation implementation and professional/native qualification are tracked separately.

<!-- work-id: PS-PRO-001 -->
### PS-PRO-001: Smart Objects and reusable components

**Status:** In progress. First slice implemented: embedded/linked image content, shared duplicates, Make Independent, asynchronous Replace Contents, inspector controls, layer/render/thumbnail wiring and history. Batch 2 adds linked Current/Missing/Changed checks, shared Relink/Update/Embed, worker-side instance preparation and stale-result/history checks. Batch 3 adds bounded editable nested documents, isolated child drafts/preview, shared Apply/Cancel, image replacement, recipe 17/provenance 4, and canonical reopen plus real PNG export checks. Batch 4 adds isolated nested editing in the ordinary canvas, per-frame selection/history, text/transform/layer/mask/paint tools, merged-artwork Object selection, inherited locks and atomic return; actual Main pointer/keyboard and controller export/reopen checks cover the delivered slice. Component source-based selections and retouch have scoped controller, real-photo reopen/export and LaMa evidence in the [selection and retouch qualification](evidence/color-range-component-retouch-2026-11-01/README.md). Shared object-table storage and authored RAW/HDR now have [final scoped Debug evidence](evidence/component-storage-layer-styles-2026-11-02/README.md); the [follow-up checklist](component-storage-and-layer-styles-qualification.md) records the remaining full-item gates. Native, camera-variety, large-document performance and full item gates remain open. See [batch 4 evidence](evidence/photoshop-professional-tools-batch4-2026-10-30/README.md). See [batch 3 evidence](evidence/photoshop-professional-tools-batch3-2026-10-29/README.md). See [batch 2 evidence](evidence/photoshop-professional-tools-batch2-2026-10-29/README.md). See [batch evidence](evidence/photoshop-professional-tools-batch1-2026-10-29/README.md).

<!-- work-id: PS-PRO-002 -->
### PS-PRO-002: Pen tool, paths and vector masks

**Status:** In progress. First slice implemented: versioned cubic paths, geometry operations, real canvas anchor/handle authoring, editable fill/stroke layers, closed-path working selections, crop/group mapping and undo/reopen routes. Batch 2 adds persistent vector masks with independent layer targets and coverage-preserving move/Alt-copy, anchor insertion/deletion and corner/smooth/handle/numeric controls, plus acknowledged/cancellable async path commits. Batch 3 adds explicit coverage-preserving Follow layer / Fixed document conversion and a declared editable SVG import/export subset with controller round-trip checks. Snapping, full boolean authoring, vector feathering, larger/tiled mask rasters, native/performance and full item gates remain open. See [batch 3 evidence](evidence/photoshop-professional-tools-batch3-2026-10-29/README.md). See [batch 2 evidence](evidence/photoshop-professional-tools-batch2-2026-10-29/README.md). See [batch evidence](evidence/photoshop-professional-tools-batch1-2026-10-29/README.md).

<!-- work-id: PS-PRO-003 -->
### PS-PRO-003: Blend If and advanced blending

**Status:** In progress. First slice implemented: source/underlying split tonal ranges, Gray/RGB, bypass/reset, coalesced inspector gestures, layer/group/adjustment rendering, persistence/cache identity and accelerated-path fallback. Fill/Opacity separation, knockout, native/HDR real-media/performance and full item gates remain open. See [batch evidence](evidence/photoshop-professional-tools-batch1-2026-10-29/README.md).

<!-- work-id: PS-PRO-004 -->
### PS-PRO-004: Shared Layer Styles

**Status:** In progress. The first slice implements ordered strokes, shadows, glows, overlays and bevel/emboss with a desktop inspector, copy/reusable preset files, mask ownership, history, versioned persistence, resource preflight and CPU/export integration. The shared [45-check Debug receipt](evidence/component-storage-layer-styles-2026-11-02/README.md) covers the delivered slice and existing selection/mask regressions. See the [acceptance checklist](component-storage-and-layer-styles-qualification.md). The next [authoring slice](hdr-retouch-style-authoring-qualification.md) adds gradient stop editing, solid/gradient strokes and independent style-mask create/edit/replace/clear, with shared paint/erase targeting and stable undo ownership. Its [51-check Debug receipt](evidence/hdr-retouch-style-authoring-2026-11-02/README.md) records the integrated run and corrected UI fixture rerun. The [canvas and lighting slice](canvas-style-and-compound-mask-qualification.md) adds preview-only shadow/gradient gestures, one-step commit/cancel, opt-in document lighting, nested-light isolation and compound selection/layer/style/effect-mask journeys. Its [scoped Debug receipt](evidence/canvas-style-light-compound-masks-2026-11-02/README.md) records production-component gestures, rendered pixels, schema negotiation and isolated catalog persistence. The [interaction cleanup](mask-layer-ui-qualification.md) adds explicit mask-to-owner return actions, rejected-transition preservation, menu ownership/lock guards, reachable layer content commands and shared UI/numeric style enforcement; see its [Debug receipt](evidence/mask-layer-ui-2026-11-02/README.md). Mounted Main/native authoring, performance and legacy text-style migration equivalence remain open.

<!-- work-id: PS-PRO-005 -->
### PS-PRO-005: Mesh Warp and Puppet Warp

**Status:** Queued.

<!-- work-id: PS-PRO-006 -->
### PS-PRO-006: Professional typography

**Status:** First dimensional batch implemented with scoped Debug/offscreen qualification: editable mesh extrusion/bevel, object/camera controls, independent four-corner perspective, source-text editing and component persistence. The [dimensional typography contract](dimensional-typography-plan.md) defines done and implementation limits. The [scoped evidence receipt](evidence/dimensional-typography-2026-11-03/README.md) distinguishes automated geometry, UI/controller, persistence and export checks from native/Release/human acceptance. Existing mixed styling, contour text, variable axes and surface materials remain foundations. Warp/envelopes, per-letter composition, kinetic type and the broader PS-PRO-006 checklist remain open.

<!-- work-id: PS-PRO-007 -->
### PS-PRO-007: Coordinated artboards and design variants

**Status:** In progress. The 2026-11-07 [professional expansion program](professional-expansion-program.md) starts with multi-format image delivery through the existing export worker: saved/undoable original, square, portrait, story and landscape selections, aspect-preserving fit/center crop, collision-safe plans, crop preflight and per-output manifest metadata. Independently editable layout bounds, shared content with local overrides, actual native workflow and full-item qualification remain open.

<!-- work-id: PS-PRO-008 -->
### PS-PRO-008: Channels and calculations

**Status:** Queued.

<!-- work-id: PS-PRO-009 -->
### PS-PRO-009: Select and Mask finishing

**Status:** Queued; preserve the independent-selection contract.

<!-- work-id: PS-PRO-010 -->
### PS-PRO-010: Actions, batch production and delivery preflight

**Status:** In progress. Multi-format image delivery is the first shared production slice in the [professional expansion program](professional-expansion-program.md), extending existing preflight, proof/manifest, cancellation and retry. Recorded parameterized actions, table-driven content replacement, portable project dependencies and full-item acceptance remain open.

<!-- work-id: PS-PRO-011 -->
### PS-PRO-011: Persistent subjects across media

**Status:** Queued; depends on proven static masks and tracking qualification.

<!-- work-id: PS-PRO-012 -->
### PS-PRO-012: One design, multiple deliverables

**Status:** Queued; depends on components and layout variants.

<!-- work-id: PS-PRO-013 -->
### PS-PRO-013: Depth-aware compositing

**Status:** Queued; extend existing scene infrastructure.

<!-- work-id: PS-PRO-014 -->
### PS-PRO-014: Animate existing artwork

**Status:** Queued; reuse existing timeline/curve infrastructure.

## Professional expansion — 2026-11-07

The user authorized all nine workstreams in the [professional expansion program](professional-expansion-program.md).
The existing item owners above retain their full acceptance gates. This table
tracks the additional scope without counting foundations as completed workflows.

| Workstream | Status | Acceptance owner / next gate |
| --- | --- | --- |
| Coordinated design variants | In progress: multi-format image delivery | PS-PRO-007; independent layout bounds and shared/local overrides remain open. |
| Reference-based shoot matching | In progress: deterministic pixel-based reference matching | Separate lighting/look amounts; rendered review with outliers unchecked; selected proposals commit as one undoable batch with stale recipe/source checks. See [qualification](shoot-match-qualification.md). Cross-camera shoots, human photographic acceptance and native/performance qualification remain open. |
| Depth-aware lighting/compositing | Queued | PS-PRO-013; shared depth, occlusion, light and shadow workflow through export. |
| Professional image restoration | Queued | Local denoise/enlargement first; declared model/runtime/source support and photographic quality review per capability. |
| Tracked subject masks | Queued | PS-PRO-011; corrective frames, confidence/occlusion and persistent segmentation output. |
| Advanced retouching | Queued | PS-PRO-008 plus editable dodge/burn, clone controls and texture-preserving workflow. |
| Mesh and puppet warp | Queued | PS-PRO-005; durable mesh/pins and direct canvas Apply/Cancel with render parity. |
| Intelligent shoot review | Queued | Extend burst/compare foundations with qualified focus/eye-state analysis and near-duplicate review. |
| Production automation and delivery | In progress: shared multi-format delivery path | PS-PRO-010; recorded actions, table-driven replacement and portable project packaging remain open. |
