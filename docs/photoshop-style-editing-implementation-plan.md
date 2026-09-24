# Photoshop-style photo editing implementation plan

Date: 2026-09-12. Status: proposed implementation plan; no product changes made by this audit.

Implementation is now in progress; see the [implementation ledger](photoshop-style-editing-implementation-progress.md) for completed work, evidence and remaining gates. The original audit findings below describe the pre-implementation baseline.

### Current-tree planning refresh — 2026-09-12

The working tree contains partial implementations that should be completed and verified, rather than replaced:

| Area | Current source evidence | Remaining implementation/release work |
| --- | --- | --- |
| Effects | `photo_effect_registry.h` registers Gaussian blur, box blur, linear-RGB invert and linear-RGB posterize. Typed instances, codecs, CPU execution, tiling and layer compilation exist. | Complete the other Phase 2 operators, performance qualification and actual preview/export/reopen proof. |
| Effect UI | `StudioWorkspace.qml` includes `PhotoEffectStack.qml`; controller commands expose add/edit, bypass, amount, reset, reorder, duplicate and selection masks. | Temporary preview with atomic Apply/Cancel, gesture coalescing, copy/paste and presets, image thumbnails, favorites and recents. Verify the visible native workflow. |
| Text | Versioned paragraph typography, installed family/face selection, fractional size, weight, italic, underline, tracking, line spacing and baseline offset exist. `photo_text_layout.h` uses shared QTextLayout rendering into RGBA64. `PhotoTextOverlay.qml` implements creation/edit drafts and Done/Cancel. | Complete point text, mixed style runs, paragraph controls, faithful canvas draft appearance, font dependency/fallback assets and actual durable export-worker proof. Verify native caret/selection/IME behavior, transforms and preview/export agreement. Legacy text retains its older rasterization semantics. |
| Groups | Group rows exist, but the inspected CPU compositor still skips group content rows. | Validated hierarchy and actual isolated/pass-through group compositing, masks and opacity semantics. |

This planning refresh inspected source only; it did not run the app or tests. Existing ledger receipts are historical evidence, not certification of the current dirty tree. The baseline table and numbered audit findings below intentionally describe the original pre-implementation audit; use this refresh for current implementation status. Phase checkboxes represent complete acceptance gates, so partial code does not check off a phase.

### Practical delivery order from this working tree

1. Finish one complete editable Gaussian workflow: temporary preview, atomic Apply/Cancel, mask editing, undo, save/relaunch and export. Verify the current CPU implementation before optimizing it.
2. Finish everyday paragraph text: accurate canvas feedback, font/face selection, wrapping and transforms, then prove identical layout through saved documents and the actual export worker. Add point text and style ranges on this shared model.
3. Expand the effect library with motion blur, high pass/unsharp mask, seeded grain, pixelate and threshold; add stack presets and image previews. Follow with radial/surface blur and canvas blur handles.
4. Add text/layer outline, shadow and glow, then shapes and gradients. These make the first practical poster/thumbnail workflow substantially more useful.
5. Implement actual group compositing, independent canvases and portable documents before promising complex graphics projects or interchange.

The phase estimates below describe the original complete scope, not a fresh estimate of remaining effort. Re-estimate remaining work after qualifying these partial implementations; source presence alone is insufficient to subtract a feature from the schedule.

## Recommendation

Build a nondestructive creative editing system around the existing photo editor: **editable filter stacks, useful blurs, direct canvas text editing, layer styles, shapes, and real grouped compositing**. The app already has substantial photographic editing and basic text/layer support. The largest gap is making those pieces behave like a coherent graphics editor.

The first useful release should let someone open a photo, blur a selected background, apply a creative look, add and position a headline, and save/reopen/export the result with every edit intact. Follow it with shadows/outlines, shapes, genuine groups, blank documents, and portable projects. Advanced lens blur, painting, warping, and Photoshop file interchange are later milestones with separate acceptance gates.

This is a plan for the **photo workspace**. Timeline effects, carousel design, and 3D Gaussian reconstruction are separate systems; their existence does not establish photo editing support. Reuse suitable components and algorithms after checking their contracts.

## Audit scope and evidence limits

Inspected the current working tree, including uncommitted work, through QML controls, controller methods, recipe serialization/adaptation, layer compilation, CPU compositing, blur helpers, render planning, persistence interfaces, export entry points, and relevant tests. No app session, build, or runtime test was executed for this planning task. “Present” below means implementation was found, not that every workflow has passed current runtime verification. “Not found” means no implementation was found in the inspected photo paths.

The older [editor upgrade plan](editor-upgrade-plan.md) is historically useful but its initial constraints are stale: curves, masks, crop, and basic text have advanced considerably. Use this document for the new creative-editing work and [capability verification](capability-verification-plan.md) for completion requirements.

### Original audit baseline: what existed and what was missing

| Capability | Current evidence | Gap and action | Priority |
| --- | --- | --- | --- |
| RAW, exposure, curves, color, grading, detail | Extensive recipe and engine implementation | Preserve and regression-test; avoid rebuilding these controls | Foundation |
| Looks and LUTs | Preset registry, service, LUT library and ordered LUT fields | Expose looks alongside creative effects without replacing unrelated edits | First release |
| Photo filter stack | No general typed, reorderable spatial-effect stack found in photo recipes | Add registry, ordered instances, masks, preview, persistence, export | First release |
| Gaussian and other blurs | Internal box, approximate Gaussian, and guided luminance blur helpers | Add actual RGBA photo effects and user controls; internal detail blur is not a blur tool | First release |
| Text | Add Text Layer menu, inspector editing, font family/size/color, regular/bold, alignment, box size; CPU rasterization | Direct canvas typing, proper layout, font picker, richer typography, output-resolution rendering | First release |
| Image, pixel, fill, adjustment layers | Layer compilation, transform, opacity, blend, clipping and mask paths present | Extend the current system; verify combinations rather than assume full parity | Foundation |
| Groups | Group rows and canonical parent field exist; inspected CPU layer loops skip group rows | Build hierarchy operations and isolated/pass-through group compositing | Second release |
| Layer effects/styles | No photo shadow, outline, glow, bevel stack found | Add nondestructive styles to text, image, shape and group content | Second release |
| Shapes and paths | No first-class photo shape/path authoring found | Add rectangle/ellipse/line/arrow, gradient fills, then pen paths | Second release / later |
| Selections and retouch | Rich mask/selection/retouch contracts already exist | Connect selections to effects and layers; assess refine-edge quality on real media | First release / later |
| Free paint layers | Pixel layer support exists; mask painting is a separate workflow | Add color painting, erasing, brush engine and tile-backed history | Later |
| Transform, layout and liquify | Affine/optical fields, simple liquify parameters, multi-selection and layer snap/align/distribute APIs exist | Extend layout to new content and real groups; eventually add warp mesh and durable liquify strokes | Second release / later |
| Smart-object-like content | Linked source layers, relink support, content asset store and canonical content identity exist | Immutable snapshots, embedded/linked policies, replace-content and nested documents | Second release / later |
| Independent document canvas | Photo source dimensions anchor existing placement paths | Add explicit canvas size, transparent background, New Document and canvas resize | Second release |
| Portable editable project | Recipe persistence and content-addressed asset storage exist | Package sources, effects, typography and dependencies for another machine | Second release |
| PSD/PSB and professional print | No layered PSD round-trip found in inspected photo paths; color/proof infrastructure exists | Interchange spike, supported-subset matrix, later print/CMYK-specific work | Separate follow-on |

### Important code findings

1. **Text is already available:** `StudioWorkspace.qml::layerAddMenuItems()` and `handleLayerAddMenuAction` dispatch expose Text Layer; `photoLayerTextEditor` edits it in the expanded layer inspector. `EditorPreviewController::addTextLayer`, `setPhotoLayerText`, `setPhotoLayerTextStyle`, and `setPhotoLayerTextBox` persist changes and submit render intents. Improve discoverability and capability rather than add another disconnected text tool.
2. **Text rendering has concrete limitations:** `photo_render_utils.cpp::rasterized_text_layer_image()` draws into an RGBA8 image of stored dimensions, defaults to a 512×128 box, clamps font size to 8–512 px, uses regular/bold and wrapped vertically centered text with fixed padding, and returns transparent content without a GUI application. This requires explicit output-resolution, font, color-precision, and worker-runtime work. This audit does not establish that current exports hit that failure branch.
3. **Grouping is organizational in the inspected renderer:** `color_layer_runtime.cpp::apply_photo_layers` and `color_fill_layer_program.cpp` skip `kind == "group"`. A folder row or a persisted parent ID alone is insufficient to implement group opacity, group masks, or isolation. The existing [layer UI checklist](layer-system-sparse-ui-checklist.md) also distinguishes this boundary.
4. **Photo layer representation is flat and adjustment-heavy:** `color_adjustments.h::PhotoLayer` contains many fixed adjustment fields, masks and raster payloads, but no general ordered spatial-effect instances. Add a dedicated model rather than more scalar properties for each new filter.
5. **Two recipe representations must be handled:** `edit_recipe_state.h` currently declares desktop recipe version 12; the engine has `EditRecipeV13` and desktop adaptation/persistence. New fields must survive both paths and render-plan identity generation. Do not assume the desktop version number means the canonical model is unused.
6. **Blur primitives are reusable, not finished effects:** `color_blur_runtime.*` operates on scalar float buffers for existing spatial processing. An RGBA filter needs explicit alpha, color-space, edge, radius, tiling, and cancellation semantics.
7. **Coordinate behavior needs unification:** source layers have a canvas-size-to-output-size scaling branch in `compile_pipeline_photo_layers`. That branch is source-specific. Check text/pixel/fill placement across reduced preview, full export, crop and rotation before defining new effect radii or text geometry.

Primary source entry points: [photo workspace](../apps/desktop/qml/workspaces/StudioWorkspace.qml), [controller](../apps/desktop/src/editor_preview_controller.cpp), [layer/text compilation](../apps/desktop/src/photo_render_utils.cpp), [layer state](../engine/include/photo_editor/pipeline/color_adjustments.h), [layer compositor](../engine/src/pipeline/color_layer_runtime.cpp), [blur helpers](../engine/src/pipeline/color_blur_runtime.h), [canonical recipe](../engine/include/photo_editor/pipeline/edit_recipe_v13.h), and [recipe adapter](../apps/desktop/src/edit_recipe_v13_adapter.cpp).

### Photoshop comparison used for scope

Photoshop's editable filter workflow supports later parameter changes, ordering, duplication, disabling and masking. Its Smart Filter mask applies to the filter stack; individual effect masks proposed here are an intentional extension. [Adobe: Smart Filters](https://helpx.adobe.com/photoshop/using/applying-smart-filters.html).

Its Blur Gallery adds field, iris, tilt-shift, path and spin blur with controls on the image. We should deliver ordinary Gaussian and motion blur first, then these more demanding spatial interactions. [Adobe: Blur Gallery](https://helpx.adobe.com/ca/photoshop/using/blur-gallery.html).

Photoshop distinguishes point text from paragraph text; editable typography needs both creation gestures and text-box reflow. [Adobe: Add text](https://helpx.adobe.com/photoshop/desktop/text-typography/get-started-with-text/add-text.html).

Shadows, glows, outlines and overlays belong to reusable layer styles, which makes them useful for photos and graphics as well as text. [Adobe: Layer style effects](https://helpx.adobe.com/photoshop/desktop/create-manage-layers/apply-layer-effects/layer-style-effects-and-options-overview.html).

## Product behavior

### Filters and blurs

- Add a **Filters** entry to the photo menu and command palette, plus an **Add effect** action for the selected layer. Use separate **Looks**, **Blur**, **Detail**, **Color**, **Stylize**, and **Distort** categories. Label existing Library search filters distinctly.
- Open a compact searchable browser with previews from the current image, favorites, recent choices, and visible target information. Generate thumbnails asynchronously and cache them; opening the browser must not launch a full-resolution render for every item.
- Selecting an effect previews it in a temporary transaction. Apply commits one history operation; Cancel/Escape restores exactly the prior recipe. Further edits to an applied effect remain nondestructive.
- Show applied effects under their owning layer, collapsed by default. Support edit, drag reorder, enable, amount, reset, duplicate, remove, copy/paste, and save stack preset. Show one focused parameter editor at a time.
- Distinguish **This layer**, **Selected area**, and **Composite below**. A selection initializes an independent effect mask; a layer mask controls layer visibility. Creating an effect without an appropriate selected layer creates a named composite-effect layer above the existing photo content.
- Background blur uses an inverted subject selection plus manual refinement. Simple masked blur and depth-aware lens blur must have different names and expectations.
- Radius and distances are defined in document pixels. Zoom changes the view, never the saved effect. Export resizing preserves the effect's proportion relative to the document.

### Text and graphics

- Add a visible **Text** tool with a context-safe T shortcut. Click for point text; drag for a paragraph box. Double-click existing text to edit it on the canvas.
- While editing, show caret, selection, wrapping and overflow. Support clipboard, Unicode input/IME, keyboard navigation, Escape cancel, and an explicit Done action. Typing must not trigger global shortcuts. Clicking elsewhere commits according to one documented focus rule.
- The context bar exposes font, size, color and alignment; advanced typography sits in a focused inspector. Moving, rotating and resizing use the same transform interaction as other objects. Resizing a paragraph box reflows text; scaling the object scales its appearance.
- Add **Style** beside text/layer properties for outline, shadow and glow. A style remains editable when the text changes.
- Add shapes through the tool rail and add-layer menu. Snapping, guides, alignment and distribution should work consistently for text, shapes and placed images.
- Preserve the existing spacious canvas and contextual UI. Extract new components rather than extending the large Studio QML file with another permanent panel stack.

## Architecture decisions

### 1. Extend the recipe with typed layer content and effects

Introduce a versioned model using stable IDs. The following names describe proposed types, not existing APIs:

```text
DocumentCanvas: width, height, pixel_aspect, resolution_ppi, working_profile,
                background, composition_mode
LayerNode: id, parent_id, kind, order, name, enabled, locks, opacity, blend_mode,
           transform, content_ref, layer_mask_ref, effects[], styles[]
EffectInstance: id, type_id, algorithm_version, enabled, amount,
                parameters, mask_ref, input_scope, coordinate_space
TextContent: utf8, point_or_paragraph, box, style_runs[], paragraph_styles[],
             font_refs[], layout_version, fallback_appearance_ref
ShapeContent: geometry, fill, stroke, fill_rule, path_version
```

Keep layer hierarchy as a validated parent relationship plus sibling order, with the panel's flat rows derived from the tree. Reject cycles, duplicate IDs and dangling required dependencies. Effects execute in saved array order; display that order explicitly. IDs remain stable across reorder, save, undo and relink. Duplicates receive new IDs and preserve or remap references according to copy scope.

Each registry entry supplies parameter types/defaults/ranges/units, algorithm version, supported content types, color domain, alpha behavior, required input bounds, CPU implementation, backend capabilities, and preset metadata. Complex effects may have custom controls; numeric controls can be schema-driven. A filter registry is not a third-party executable plugin loader.

Introduce an explicitly versioned successor for each affected serialized representation after auditing readers. Do not silently change the meaning of v12 or v13. Extend validation, normalization, equality, diff, hashing, migration, clipboard, snapshots, presets, persistence, preview and export together. Preserve unsupported future payloads where possible and refuse a faithful export if an active unsupported effect cannot render; never silently replace it with an adjustment or omit it.

Old recipes load with an empty new effect stack and their original render semantics. Do not migrate every catalog record or change the existing photographic renderer automatically. Save an upgraded representation only on an explicit document edit/save, with recovery data and compatibility metadata.

### 2. Add explicit effect and compositing stages

Reuse the canonical recipe compiler, render executor, invalidation policy and tile planner. Keep the user interface as layers and effects; the internal render graph handles dependencies.

For new layer-local effects, define this sequence:

```text
source decode / text-or-shape rasterization
  -> input color conversion and existing source adjustments
  -> ordered content effects in layer coordinates
  -> layer transform into document coordinates
  -> layer mask and clipping coverage
  -> styles derived from the resulting silhouette
  -> layer/group opacity and blend into parent
```

Composite-effect layers instead read the accumulated siblings below them within the current group, run their stack in document coordinates, and blend the result through their effect coverage. They must never recursively include themselves or layers above them. Existing photographic adjustment order remains versioned and unchanged for legacy documents.

Group mode is explicit: **isolated** renders children into an intermediate, then applies the group's mask/effects/opacity once; **pass-through** permits child adjustments to affect the enclosing backdrop. In the first group release, groups with their own effects/styles/masks use isolated mode with visible UI behavior. Do not implement group opacity by multiplying every child's opacity; overlapping content produces different pixels.

An effect mask mixes its input and effect output in premultiplied form: `result = input * (1 - coverage * amount) + effect_output * (coverage * amount)`. A blur may still sample beyond the effect-mask edge. “Blur only within selection” requires a separate sampling policy; it cannot be inferred from that mix equation. A layer mask is applied separately to final layer visibility.

### 3. Preserve photographic color and graphic color intentionally

- Spatial photographic operations use float linear working RGB by default. Explicitly convert for perceptual/artistic operators that need a different domain. Preserve finite HDR values and negative working values when their algorithm permits them.
- Convolution operates on premultiplied color and alpha, with safe unpremultiplication at the existing straight-alpha boundary. Define transparent outside bounds for cutout layers and explicit extend/reflect policies for opaque photo edges. Test hidden RGB under zero alpha.
- Preserve the legacy mode where layers participate in the photographic pipeline. For new design documents, introduce an explicit graphic-composition mode: develop photographic content through its chosen photographic output transform into a common linear document/output-reference space, composite intended graphic colors there, then perform final proof/device/output encoding. Text swatches must not accidentally acquire a second camera look or output tone map.
- That second mode is a phase-gated architectural addition, not an automatic conversion of existing edits. Verify color swatches, blend modes, SDR/HDR behavior and mixed RAW/JPEG/text documents before enabling it. Defer unsupported HDR design combinations with a clear reason.
- Preview/export comparisons use a common output target. A monitor screenshot and a differently profiled export are not a valid direct numerical comparison.

### 4. Make filters scale correctly and render within a memory budget

- Every effect declares its output bounds and required input region. Gaussian finite support, motion direction, glow expansion and displacement dependencies feed tile padding. Accumulate support through a stack; one global blur padding value is insufficient.
- Store physical geometry in document or explicitly declared layer coordinates. Compose source, crop, rotation, object, preview and export mappings once. Handle nonuniform transforms without reducing them to a single guessed radius factor.
- Use separable Gaussian with documented sigma/support as the CPU reference. Existing repeated-box approximation may be an interactive implementation only after error characterization; do not call unlike kernels numerically identical.
- Cache per input revision, effect parameters/version, mask revision, color configuration, scale and quality. Editing the final effect should reuse earlier stages. Local changes invalidate affected tiles and downstream consumers.
- Reuse render cancellation and generation-based publication. A completed stale render must not replace a newer document. Debounce persistence independently of visual feedback.
- Bound resident CPU/GPU memory and scratch use. A single 24-megapixel float RGBA buffer is about 384 MB; keeping several for every layer is untenable. Add eviction and disk-backed tiles where required. Broad-radius effects may use measured multiresolution methods with a distinct quality contract.
- CPU is the correctness baseline on both macOS and Windows. Metal acceleration needs actual device dispatch and parity evidence. Windows must have an accepted CPU performance profile or a separately planned GPU backend; Metal does not cover it.

### 5. Treat text as editable content, with derived rendering

Persist text, runs, paragraph layout, font identity and geometry, never only a preview bitmap. Use a shared typography service initially based on Qt's text layout/shaping facilities; prototype its offscreen worker behavior before committing the model. The service produces output-scale raster/coverage assets for the Qt-free compositor. Keep desktop editing and export on the same layout implementation.

Use `QTextLayout` for layout, runs and cursor geometry and `QFontDatabase` for installed face discovery. Supply explicit paint-device/document metrics so layout does not inherit the current screen's metrics. Verify APIs against the repository's pinned Qt version before implementation. [Qt: QTextLayout](https://doc.qt.io/qt-6/qtextlayout.html), [Qt: QFontDatabase](https://doc.qt.io/qt-6/qfontdatabase.html).

Font identity includes face/style, file fingerprint where available, and variable-font axes when supported. A font picker previews actual installed faces and reports missing fonts. Do not silently substitute a different font and claim unchanged layout. Store a fallback appearance suitable for reopening with stable visual output; retain editable text for explicit font substitution. Package font data only where embedding is allowed; otherwise package a frozen appearance and dependency report.

Text rendering must work in the actual export worker runtime. For the initial Qt implementation, supply the required offscreen `QGuiApplication` and font resources or an already-resolved immutable appearance; a `QCoreApplication`-only route must fail clearly instead of returning invisible text. Test this through the real worker invocation, not only a UI process. Choose outline/display-list fallback or sufficient-resolution raster fallback during the typography spike and record fidelity/size limits before Phase 4 ships.

Use higher-precision color/coverage intermediates rather than baking graphic colors through RGBA8 unnecessarily. Rasterize at requested resolution with bounded caching; retain fractional glyph positions and preserve layout when zooming. Never use a screenshot of the QML text editor as export content.

### 6. Reuse persistence and history, then add portable documents

Extend `RecipeAssetStore`, canonical content identities, the save queue/journal, history snapshots, and export recipe handoff. Store large pixel/mask/font-appearance assets by content identity instead of copying base64 data into every undo step.

Every gesture or effect dialog is one logical undo transaction. Save/relaunch, autosave recovery, queued exports and copied layer stacks resolve the same immutable dependencies. Exports capture a document snapshot at enqueue time, so later edits or linked-file changes cannot alter an already queued result.

Portable projects add a manifest, versioned recipe, referenced assets, preview, dependency/license report and atomic publication. Use the existing catalog as an index to documents; saving a document must not reset or replace catalog authority. Linked versus embedded is an explicit per-source policy. Garbage collection must retain assets referenced by documents, history, queued jobs and recovery journals.

## Delivery phases and acceptance gates

Each phase includes implementation, meaningful tests, real-image evidence and manual interaction checks. Checkboxes below are planned work, not completed work.

### Phase 0 — Capture the baseline and settle contracts

- [ ] Create a focused capability ledger for photo filters/text/groups with current entry points and evidence level.
- [ ] Capture workflows for existing text, image layers, masks, clipping, undo, save/reopen and export using isolated test catalogs.
- [ ] Reproduce text behavior at reduced preview and full output, rotated/cropped documents, missing fonts, and the actual export worker.
- [ ] Confirm all recipe readers/writers and choose new versions; specify layer/effect coordinate spaces, stage order and migration fixtures.
- [ ] Prototype one large-radius RGBA blur and one mixed-script paragraph at 24 MP to expose renderer/font/runtime risks early.
- [ ] Record reference hardware, memory limits, fixture hashes and measured latency before setting final budgets.

**Exit:** baseline receipts and architecture decisions exist; every known regression is classified. Existing catalog data remains intact. Source inspection alone cannot mark the current text or group workflow complete.

### Phase 1 — Ship the effect framework with one real blur

- [ ] Add typed effect instances/registry, versioned codecs, validation, adapter support and cache identities.
- [ ] Add layer-local and composite-below scope, effect mask references and explicit execution order.
- [ ] Implement Gaussian RGBA CPU reference, cancellation, radius scaling, bounds expansion and tile halo propagation.
- [ ] Add a compact Gaussian inspector and stack row with edit, bypass, amount, reset and remove.
- [ ] Connect selections to effect masks, and implement preview/apply/cancel as history transactions.
- [ ] Route final preview, exports, thumbnails and reopening through the same compiled effect model.

**Exit:** open a real photo → add Gaussian blur → mask it → edit radius → undo/redo → save → relaunch → export. Radius 0 is identity; transparent edges have no dark fringe; tiled/full outputs agree; unsupported payloads are never dropped silently. This milestone can ship behind an experimental feature flag before the full browser is ready.

### Phase 2 — Essential filters and a usable browser

| Effect | First implementation | Core checks |
| --- | --- | --- |
| Gaussian blur | Sigma/radius, documented support, alpha/edge mode | Impulse response, constant image, transparency, scale, tiles |
| Box blur | Radius, normalized separable box kernel | Exact small reference and edge handling |
| Motion blur | Distance, angle, centered/directional trail | Correct direction, endpoints, halo bounds |
| Radial spin / zoom blur | Center, angle or strength, quality | Center stability, angular sampling, deterministic refinement |
| Surface blur | Radius and edge threshold, documented edge-preserving algorithm | Detail retention and edge leakage corpus; distinguish from Gaussian |
| High pass / unsharp mask | Radius, amount, threshold where applicable | Neutral response, signed detail, halos, HDR limits |
| Noise / grain | Amount, size, monochrome/color, seed | Same seed survives reopen and tile boundaries |
| Pixelate / mosaic | Cell size, grid origin | Stable document anchoring and export scale |
| Posterize / threshold / invert | Levels or threshold and named color domain | Exact boundary fixtures and predictable neutral ramps |
| Vignette / bloom / glow | Geometry or threshold, softness/radius, amount | Highlight rolloff, alpha, crop and edge expansion |
| Creative looks | Existing LUT/preset engine, visible amount and target | Declared LUT domain; no unrelated recipe replacement |

- [ ] Deliver browser categories, search, current-image previews, favorites and recents.
- [ ] Add stack reorder/duplicate/copy/paste, presets and missing-effect reporting.
- [ ] Bundle a small curated set of looks made from shipped operators; label them as presets, not distinct algorithms.
- [ ] Add measured Metal implementations for Gaussian, motion, point effects and other proven bottlenecks. Keep truthful CPU fallback.

**Exit:** effects visibly combine in saved order and survive export/relaunch; reordering blur and threshold proves the order actually changes pixels. Browser navigation remains responsive. Heavy radial/surface effects can be labeled slower until their measured interaction gate is met.

### Phase 3 — Blur controls on the image

- [ ] Tilt-shift: movable focus band, angle, near/far transitions and amount.
- [ ] Iris blur: center, ellipse axes, rotation and feathered focus region.
- [ ] Field blur: multiple pins with independently editable radii and specified interpolation.
- [ ] Shared handle hit-testing, numeric entry, keyboard nudges, reset, cursor/focus and undo behavior.
- [ ] “Blur background” action using existing subject/background selection and refinement, with visible mask controls.

**Exit:** handles track the image at all supported zoom/rotation levels; mask preview corresponds to rendered softness; pin/band edits survive reopening. Portrait hair and high-contrast boundaries are inspected. These tools are creative variable blur, not a claim of physically accurate lens simulation.

### Phase 4 — Make text a complete everyday tool

- [ ] Add Text tool, click/drag creation, on-canvas editing, caret/selection, clipboard and IME-safe shortcuts.
- [ ] Introduce versioned `TextContent` and migrate existing text while preserving its old layout until the user opts into the richer layout model.
- [ ] Add searchable font picker, actual family/style faces, fractional size, italic, underline, tracking, line spacing and baseline offset.
- [ ] Support paragraphs, left/center/right/justified alignment, explicit box padding, overflow indicator, point/paragraph conversion and mixed style runs.
- [ ] Add shared transform handles, reflow versus scaling, alignment/snapping, duplicate and editable text presets.
- [ ] Implement requested-resolution typography service, worker font setup, missing-font/fallback behavior and text color precision.

**Exit:** make a headline and wrapped caption; edit words directly; style a range; resize/reflow; rotate; copy/paste; undo/redo; save/reopen; export at several sizes. Verify Latin, combining marks, CJK, Arabic/RTL, missing fonts and IME input. Export has the same line breaks, glyph positions and colors within documented rasterization tolerances. No text vanishes in a worker process.

**First release checkpoint:** Phases 0–2 and 4 plus shared quality gates. Phase 3 may follow as an incremental blur release. The user can blur a background, add creative filters and editable text without waiting for PSD, vector pen tools or a complete document redesign.

### Phase 5 — Layer styles, basic shapes and layout

- [ ] Add typed style instances: drop shadow, inner shadow, inside/center/outside outline, outer/inner glow, color overlay and gradient overlay.
- [ ] Define style silhouette, mask interaction, opacity/fill-opacity distinction, compositing order, bounds and scale behavior. Defaults should work on text immediately.
- [ ] Add rectangle, rounded rectangle, ellipse, line and arrow; solid/linear/radial gradient fills; stroke width, joins and caps.
- [ ] Extend existing multi-selection, snap, align/distribute and transform commands to text/shapes and shared bounds; add missing rulers/draggable-guide interactions and verify keyboard nudge/numeric entry.
- [ ] Save/copy/paste styles independently of layer content. Add gradient stop editor with explicit interpolation space.

**Exit:** produce a photo poster with editable headline, outlined caption, soft shadow, gradient panel and arrow. Changed text updates its styles; shadows are not clipped by old raster bounds; alignment and output colors remain correct at 1× and 2× export. Bevel/emboss, satin and pattern overlays are a later extension after the simpler styles pass.

### Phase 6 — Real groups and independent documents

- [ ] Implement parent/sibling commands, drag into/out of groups, group/ungroup, recursive duplicate, visibility/lock propagation and collapse state; extend existing multi-layer selection to hierarchical operations.
- [ ] Implement isolated group rendering, masks, opacity and styles once per group; add pass-through semantics and validate clipping chains within parent boundaries.
- [ ] Preserve old organizational rows on load; never invent child membership from visual row adjacency alone.
- [ ] Introduce explicit document canvas, New Document presets/custom size, transparent/solid background, resize canvas with anchor, image resampling and resolution metadata.
- [ ] Decouple document identity from a mandatory source photo. Update catalog projections, recent documents, save queues and export dimension estimation for source-less documents.
- [ ] Deliver the explicit graphic-composition color mode and regression fixtures described above.

**Exit:** start from a blank transparent canvas, place two photos plus text/shapes, group them, apply group opacity/mask/effect, resize canvas, save/reopen and export. Overlap tests distinguish correct group opacity from per-child multiplication. Existing photo recipes retain baseline pixels.

### Phase 7 — Portable projects and replaceable content

- [ ] Extend the content asset store with immutable source snapshots and asset-reference accounting.
- [ ] Add embedded/linked choice, replace content while preserving transforms/effects, relink and refresh with explicit content revision.
- [ ] Write/read a native portable document package using a versioned manifest and atomic save. Choose the extension during the package-format spike; do not repurpose image export as document save.
- [ ] Include fonts or fallback appearance, masks, LUTs, seeds, sources and thumbnails as required; show unresolved dependencies before sharing/export.
- [ ] Add crash recovery, interrupted-save and disk-full handling, safe package extraction, queued-export snapshots and asset retention across history.
- [ ] Add nested reusable content documents only after cycle detection, dependency invalidation and independent save semantics pass.

**Exit:** copy a packaged project to another machine, disconnect original source paths and reopen with matching appearance. Editable text persists when the required font exists; otherwise the documented fallback is visible. No catalog reset is needed. A queued export stays tied to its original snapshot after a source file is replaced.

**Second release checkpoint:** styles, shapes, groups, blank documents and portable projects make the app useful for posters, thumbnails, composites and social graphics as well as photo adjustments.

### Phase 8 — Advanced photographic and artistic effects

- [ ] Depth/lens blur: imported or locally generated depth, focus distance, near/far strength, aperture blades/rotation, highlight response and quality controls.
- [ ] Handle foreground/background occlusion with depth layers or another specified method; expose depth correction. Masked Gaussian is insufficient for hair and foreground silhouettes.
- [ ] Path blur with editable paths and velocity/taper; advanced spin controls.
- [ ] Distortions: ripple, wave, twirl, displacement map; reusable bounded sampling and inverse-coordinate mapping.
- [ ] Stylization: halftone, edge detection, emboss, sketch and oil-paint-like treatment, each with a named algorithm and fixtures.
- [ ] Extend layer styles with bevel/emboss and pattern overlays; advanced typography with text on path, warp and variable axes.

**Exit:** effect-specific numerical and visual reference tests, transparent/HDR/large-image coverage, persistent seeds/assets and measured performance. Generated depth is optional and locally controlled; model availability does not block ordinary blur or text.

### Phase 9 — Painting, paths, selection quality and deformation

- [ ] Color paint/erase layers with pressure, flow, hardness, spacing, smoothing, brush tips, blend modes and sample-all/current-layer behavior.
- [ ] Tile-backed raster storage and undo deltas; preserve replayable strokes where practical. Mask brushes and color brushes share input handling but keep distinct state and semantics.
- [ ] Pen/Bézier paths, handles, boolean shape operations, vector masks, selection-to-path/path-to-selection and persistent alpha selections/channels.
- [ ] Free transform/perspective/warp mesh and liquify stroke field with freeze/thaw, reconstruct and bounded inverse sampling; integrate existing simple liquify controls compatibly.
- [ ] Audit existing clone/heal/remove on real difficult fixtures; add non-destructive retouch layers and frequency-separation workflow where the audit finds gaps.
- [ ] Add actions/macros from typed commands with replay, parameters, progress, cancellation and batch export. Extend local-agent editing only through the same validated commands and existing review rules.

**Exit:** practical composite/retouch workflows are verified with pointer, keyboard and tablet input on supported hosts. Painting a long session has bounded memory and survives recovery; undo removes one stroke without damaging neighboring tiles.

### Phase 10 — Interchange and specialist professional features

- [ ] Inventory image writer/reader and library support; choose a maintained PSD strategy through a bounded compatibility spike.
- [ ] Start with documented layered raster interchange, such as an evaluated OpenRaster route, and flattened PSD preview import if supported. Report every rasterized or unsupported feature.
- [ ] Add a tested PSD subset incrementally: raster layers, positions, opacity/blend subset, groups, masks; then text/styles only when round-trip fixtures prove fidelity. PSB, smart objects and arbitrary Photoshop effects are separate commitments.
- [ ] Build interoperability fixtures that open in Photoshop and reimport; preserve native projects as the authoritative editable format until that matrix passes.
- [ ] Evaluate artboards, templates, linked reusable assets, print layout, CMYK separations, spot channels and print output against actual customer workflows. Existing soft-proof code is not proof of CMYK document editing or press-ready output.

**Exit:** publish a precise compatibility matrix. Never advertise general PSD compatibility because a flattened image opens. Full Photoshop parity, Adobe plugins and cloud generative features are outside the initial releases.

## Repository implementation map

Paths below exist unless explicitly labeled proposed. Keep changes focused and preserve unrelated working-tree edits.

| Responsibility | Existing integration points | Proposed additions |
| --- | --- | --- |
| Recipe/model | `apps/desktop/src/edit_recipe_state.h`, `edit_recipe_layer_codec.cpp`, `edit_recipe_document_codec.cpp`, `edit_recipe_v13_adapter.cpp`, `edit_recipe_v13_persistence.cpp`; `engine/include/photo_editor/pipeline/edit_recipe_v13.h`, `engine/src/pipeline/edit_recipe_v13.cpp` | Versioned effects, text, canvas, styles and hierarchy model/codec modules; do not place every new implementation in existing giant files |
| Commands/session | `editor_preview_controller.*`, `layer_session.h`, `history_service.h`, `edit_history_snapshot_store.*` | `photo_effect_controller.*`, `photo_text_controller.*`, `photo_document_commands.*`, typed list models |
| Compilation/render | `desktop_pipeline_state_compiler.cpp`, `photo_layer_compiler.h`, implementation in `photo_render_utils.cpp`; `recipe_compiler.*`, `render_executor.*`, `color_layer_runtime.cpp`, `color_fill_layer_program.cpp` | Effect compiler/executor, hierarchical compositor, reusable typography/content preparation service |
| Algorithms | `color_blur_runtime.*`, `color_spatial_runtime.*`, `color_adjustments.h`, `metal/color_adjustments.metal`, `metal/metal_pipeline_backend.mm` | `engine/src/pipeline/effects/`, per-effect CPU implementations and Metal kernels; bounded scratch/cache interface |
| Invalidation/tiles | `render_invalidation_policy.*`, `render_tile_planner.*`, desktop `render_plan_store.*`, `render_scheduler.h` | Per-node dependency bounds, effect-quality identities, tile-cache accounting |
| UI | `StudioWorkspace.qml`, `Main.qml`, `ToolRail.qml`, `LayerRow.qml`, `LayerTransformOverlay.qml`, shared inputs/popups | `PhotoEffectBrowser.qml`, `PhotoEffectStack.qml`, `PhotoEffectInspector.qml`, `PhotoTextOverlay.qml`, `PhotoTypographyPanel.qml`, `PhotoLayerStylePanel.qml`, `PhotoShapeOverlay.qml`, `PhotoCanvasDialog.qml` |
| Presets | `preset_registry.*`, `preset_service.*`, existing LUT parsing/library functions | Versioned creative stack and style presets; target-aware apply semantics |
| Persistence/export | `recipe_asset_store.*`, `editor_recipe_save_queue.*`, `editor_recipe_save_journal.*`, `desktop_recipe_persistence_service.*`, `export_job_controller.cpp`, `flattened_output_renderer.h`, implementation paths in `photo_render_utils.cpp`; Rust catalog/jobs | Package service, immutable job dependencies, text runtime initialization, new document projections |
| Tests/build | `tests/CMakeLists.txt`, desktop/engine CMake targets, existing recipe/layer/export tests | Focused effect, typography, hierarchy, document and package suites; actual-app proof fixtures |

Use typed services and small QML components to keep the controller and Studio workspace from growing further. Extract only the code needed for a milestone, with behavior-preserving tests; do not make an app-wide rewrite a prerequisite for the first blur.

## Suggested reviewable work packages

| Package | Deliverable | Depends on |
| --- | --- | --- |
| 01 | Baseline fixtures, evidence ledger and coordinate/stage decisions | — |
| 02 | Versioned effect schema, migration, hashing and unsupported-feature behavior | 01 |
| 03 | Gaussian CPU reference, bounds, alpha and tile tests | 01 |
| 04 | Effect compiler/executor and shared preview/export integration | 02, 03 |
| 05 | Stack commands, masks and atomic undo/persistence | 04 |
| 06 | Gaussian UI and complete native workflow proof | 05 |
| 07 | Filter browser and versioned stack presets | 06 |
| 08 | Essential point/detail effects and motion blur | 04, 07 |
| 09 | Radial/surface blur and measured acceleration | 08 |
| 10 | Versioned text model and typography worker spike | 01 |
| 11 | Font picker, rich layout, output rendering and migration | 10 |
| 12 | Canvas text editing, transform, accessibility and workflow proof | 11 |
| 13 | Iris/tilt-shift/field controls and selection integration | 06, 09 |
| 14 | Layer styles and silhouette/bounds tests | 04, 11 |
| 15 | Basic shapes, gradients and alignment tools | 14 |
| 16 | Group model/commands and recursive state tests | 02 |
| 17 | Group compositor, masks and isolation proof | 04, 16 |
| 18 | Explicit canvas, blank document, graphic color mode | 12, 15, 17 |
| 19 | Immutable linked/embedded assets and native package | 18 |
| 20 | Recovery, cross-machine proof and release qualification | 19 |
| 21+ | Advanced blurs, painting, paths, warp, interchange | Appropriate earlier gates |

Text work can proceed independently of most filter work after shared coordinate/history contracts are settled. Styles reuse blur and text foundations. Portable projects depend on an explicit document model. These are dependency opportunities, not a claim that parallel staffing is available.

## Verification and performance requirements

### Required correctness coverage

- **Math:** identity/disabled effects, constant fields, impulses, known small convolutions, finite values, seed determinism, alpha-zero hidden RGB, negative/HDR values and kernel energy. Compare against an independent small reference where feasible, not two wrappers around the same implementation.
- **Composition:** transparent colored edges, overlapping translucent children, clipping chains, mixed blend modes, group masks, mask-before/after semantics, effects extending outside source bounds and nested groups.
- **Geometry:** image orientation, cropped/rotated views, 25/50/100/200% zoom, fractional placement, export sizes, off-canvas content, transformed text, tile seams and wide-radius halos.
- **State:** apply/cancel, reorder, duplicate/remap, coalesced gestures, reset, undo/redo, unsupported versions, missing assets, save/reopen, crash recovery and snapshot consistency while an export runs.
- **Typography:** kerning, ligatures, multiline reflow, mixed style runs, combining marks, RTL/CJK, glyph fallback, emoji policy, missing fonts, fractional sizes, transformed baselines and actual worker execution.
- **UI:** real pointer interaction, keyboard-only paths, IME, focus/shortcut ownership, collapsed inspectors, long translated labels, high DPI and no overlap. Do not substitute a QML property signature for visual proof.
- **Color/output:** JPEG, transparent PNG, 16-bit output, tagged wide-gamut images, real RAW sources and supported HDR output. Match reference and output profiles before measuring.

Extend relevant existing tests such as `edit_recipe_state_test`, `edit_recipe_v13_test`, `edit_recipe_v13_adapter_test`, `edit_recipe_v13_persistence_test`, `editor_preview_math_test`, `render_parity_test`, `photo_layer_alpha_test`, `photo_layer_sampling_test`, `render_tile_planner_test`, `export_recipe_handoff_test`, `catalog_recipe_reopen_test` and `photo_export_recovery_test`. Add behavior-specific suites, for example `photo_effect_stack_test`, `photo_blur_reference_test`, `photo_effect_tile_parity_test`, `photo_text_layout_test`, `photo_text_export_test`, `photo_group_composite_test` and `photo_document_package_test`.

### Provisional performance targets

These are proposed acceptance targets, not measured current performance. Phase 0 must bind them to named macOS/Windows hardware, document dimensions, effect/radius ranges and build configuration.

| Scenario | Proposed target |
| --- | --- |
| Caret, selection, transform handles, slider thumb | p95 UI frame work within 16.7 ms on a 60 Hz reference display |
| Ordinary filter change at a 2048 px long-edge interactive preview | p95 first changed pixels within 100 ms; latest generation only |
| Gaussian up to 50 document px, 24 MP source, one affected layer | Settled 2048 px preview within 300 ms after input stops on reference accelerated Mac |
| Existing text content edit | Immediate editing feedback; updated composited preview within 100 ms under the named representative document |
| Warm filter browser | Opens within 100 ms; thumbnails fill asynchronously without blocking input |
| Full-resolution export / heavy lens or path blur | No invented universal deadline; record p50/p95, cancellation latency, memory and throughput per fixed fixture |
| Memory | Explicit configurable CPU/GPU/tile budgets; test eviction under pressure; no full-frame allocation per history step |

Targets must distinguish UI response, draft pixel feedback, settled preview and full export. Heavy effects can use a measured draft during interaction and deterministic refinement on release, but must not visibly change composition or color intent. CPU fallback receives its own measured latency table.

For numerical parity, define per-operator tolerances before optimizing: exact identity/point tests where feasible; Gaussian scalar CPU reference target around `1e-5` absolute on normalized SDR fixtures; GPU tolerance established against that reference and documented per algorithm. Use absolute and relative tolerances for HDR, plus perceptual/golden checks. Do not widen tolerances solely to pass an optimization. Approximate draft kernels have separate named tolerances and never stand in for final parity.

### Evidence required to release a capability

1. Run the smallest relevant native targets and state/controller suites; then the appropriate broader regressions. Use the repository CMake presets. Run Rust workspace tests when Rust changes.
2. Run QML lint independently of the tests for modified UI components.
3. Run actual-app workflows with real licensed fixtures and isolated catalog/cache settings. Capture unobscured screenshots/recordings and final exports.
4. Run healthy persistence save/relaunch checks and real queued-export worker checks, including missing asset/font failures.
5. Run CPU tests on both supported platforms and actual Metal dispatch/parity/performance on a Mac with a Metal device. A skipped hardware test is an open gate.
6. Store commands, commit/working-tree fingerprint, fixture hashes, device/backend, timings, memory, output comparisons and limitations in a dated evidence directory. Confirm visible layout and output quality manually.

## Effort, risks and release boundaries

Rough planning ranges for one experienced engineer familiar with this codebase, including focused tests and workflow proof but excluding dedicated design/QA staffing: Phase 0 **1–2 engineer-weeks**; Phases 1–2 **7–12**; Phase 3 **3–5**; Phase 4 **5–9**; Phase 5 **5–8**; Phase 6 **6–10**; Phase 7 **4–7**. Thus the first filters/text release is roughly **13–23 engineer-weeks** (0–2 and 4), with the broader second release another **15–25** (5–7). Advanced blur, painting, deformation and interchange require separate estimates after their spikes. These are scope estimates, not delivery dates; foundational defects discovered in Phase 0 can change them substantially.

| Risk | Mitigation / release decision |
| --- | --- |
| Schema extension disappears in one adapter/export path | Round-trip and real export tests before UI rollout; strict unsupported-feature behavior |
| New effect order changes old edits | Versioned legacy semantics and golden fixtures; explicit upgrade only |
| Huge radii or many layers exhaust RAM/VRAM | Bounded tiles/scratch/cache, halo planning and realistic large-image benchmarks |
| Text looks correct in UI but vanishes/reflows on export | Shared typography implementation, real worker tests and font identity/fallback |
| Text/shapes shift color under photographic tone mapping | Explicit composition modes and swatch/output fixtures |
| Group controls imply behavior the renderer lacks | Ship hierarchy controls together with group compositing evidence |
| Subject blur leaks foreground color | Separate mask coverage from sampling/occlusion; defer lens claims until depth tests pass |
| Linked file changes corrupt reproducibility | Immutable content revisions and export snapshots; explicit refresh |
| New controls overwhelm Studio | Contextual components and one focused editor; native visual QA |
| PSD consumes the whole schedule | Native editable format first; bounded subset and published compatibility matrix |

The recommended next implementation is **reconcile Package 01 evidence with the current tree, finish the existing Gaussian workflow through Packages 02–06, then complete the text-model spike and Packages 10–12**. Reuse the partial effect stack, font picker and shared text rasterizer described in the refresh above; do not build duplicate systems. That establishes a reusable route from visible controls through saved state to correct exported pixels. It also gives users a useful improvement early, while deeper Photoshop-style features build on tested foundations.
