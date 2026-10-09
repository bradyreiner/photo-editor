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
