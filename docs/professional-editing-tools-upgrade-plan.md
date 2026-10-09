# Professional editing tools: current audit and upgrade plan

Date: 2026-09-19. Scope: photo-workspace masking, selections, brushes, erasers, retouch/inpainting, text, filters and blurs.

Creative direction update (2026-09-24): [the creative tools plan and completion metrics](creative-tools-plan.md) defines nine proposed filter, typography, and brush families focused on analog print, experimental photography, and sculptural lettering. It includes current foundation evidence, delivery order, visual review, native reliability, performance, and UI/agent parity gates. These are planned additions, not completed capabilities.

Implementation update: [the first brush/text/targeting fixes and evidence](evidence/editing-tools-2026-09-19/README.md) now supersede the corresponding audit observations below. Execution is consolidated per the user's direction: fix concrete defects immediately, complete related tool families together, and validate complete workflows. The detailed phase table is a dependency/reference map, not a requirement for separate planning and benchmarking passes.

Pixel painting update: [actual color paint layers, pixel erasing and native controls](evidence/pixel-paint-2026-09-19/README.md) supersede the audit's missing-color-paint observation. The subsequent [float tile engine and native performance evidence](evidence/paint-engine-2026-09-19/README.md) adds asynchronous stroke/selection work, shared unchanged tiles in undo, versioned high-precision storage and safe queued strokes/navigation. Native 24 MP first-presented feedback is still 401 ms median (442 ms on an upscaled photograph); rendering latency, tablet qualification, long sessions and broader brush capabilities remain open.

Preview/retouch update: [regional paint publication and retouch evidence](evidence/paint-preview-retouch-2026-09-19/README.md) supersedes the latency figures above for eligible pixel-paint documents: 33.5 ms median first feedback on the comparable 24 MP provider benchmark, and 35.3 ms on a photograph through the actual Metal texture item. Complete long strokes remain about 134–136 ms. Clone strokes now retain independent source transforms; healing uses a linear-time distance transform; inpainting supports active cancellation, aspect-preserving model context and 16-bit patches. RAW/complex-document latency and native-detail reconstruction beyond the 512-pixel model remain open. The broad UI shard and QML warning-budget failures are recorded separately in that evidence.

Filter/blur update: [motion blur, unsharp masking and effect workflow evidence](evidence/photo-effects-2026-09-19/README.md) extends the initial four-operator inventory below to six. Both new operators have fractional controls, masks, preview Apply/Cancel, history and canonical persistence. Dedicated GPU execution and large-radius performance remain open; richer text, selection/mask quality, brush/eraser capabilities, high-resolution retouch and group compositing remain part of the same roadmap.

Typography update: [point text, paragraph controls and native photographic evidence](evidence/typography-2026-09-19/README.md) supersedes the missing point-text and paragraph-spacing observations below. Click creates auto-sizing point text; drag creates a paragraph box. Paragraph spacing, justified alignment and missing-font warnings preserve undo and canonical persistence. Native draft/committed glyph coverage matched, and exports matched after a saved recipe was reopened. Mixed style ranges, outline/shadow/glow, full Studio workflow acceptance and operating-system IME qualification remain open.

This is a supporting specification for the active [mask/layer/selection owner](mask-layer-selection-checklist.md). Performance qualification belongs to [performance measurement](performance-measurement-expansion-checklist.md), and final acceptance belongs to [capability verification](capability-verification-plan.md). It extends the earlier [Photoshop-style editing plan](photoshop-style-editing-implementation-plan.md) with current source findings and the user's broader brush, eraser and retouch requirements. It does not independently mark implementation work complete.

## Recommendation

Finish the shared interaction, targeting and rendering contracts first, then deliver complete professional workflows for each tool. Preserve the existing mask engine, asynchronous commits, recipe persistence, effect stack and typography work. The immediate priority is predictable, responsive editing; the next is filling actual capability gaps, especially color painting, pixel erasing, richer effects and typography.

The first release gate is a complete editing session: select a subject, refine difficult edges, remove a distraction, paint and erase on a separate layer, blur the background, add styled text, undo/redo, save, relaunch and export. Every operation must affect the intended target and survive that entire workflow.

## Evidence and limits

- Inspected the current dirty working tree, including uncommitted implementations, across QML input, controller commands, brush rasterization, model inference, typography, effect execution and layer compositing. Unrelated changes were preserved.
- Built nine focused targets and ran their nine CTest entries successfully: `brush_rasterizer_test`, `brush_stroke_sampling_test`, `mask_session_test`, `canvas_coordinate_mapper_test`, `photo_text_style_test`, `photo_text_layout_test`, `photo_text_overlay_test`, `photo_effect_gaussian_test`, `photo_effect_codec_test`. Total CTest time: 2.44 seconds. Some checks are source contracts or isolated harnesses; these are not nine complete user workflows.
- No full native app walkthrough, real-image quality comparison, installed-model inference, tablet test, export/relaunch exercise, or current latency benchmark was performed for this audit. Historical receipts and checked boxes do not qualify this entire current tree.
- “Confirmed” below means directly observed source behavior. Performance consequences are hypotheses until timed on the actual input-to-display path. “Missing” means not found in the inspected photo paths, not absent from every workspace.
- Carousel has its own brush/eraser stroke model. Its existence does not establish color painting in the photo workspace. Likewise, timeline and 3D effects are not photo effects.

## What exists, and what needs work

| Tool | Existing implementation | Professional gap / next action |
| --- | --- | --- |
| Masking | Multiple mask sources, combinations, inversion, density/opacity, feather/refinement, range masks, layer attachments, previews/outlines, caches and history. | Prove final alpha, visible outline and affected pixels agree across transformed views and export. Qualify hair, fur, glass, holes and color spill; preserve fine features when simplifying display geometry. |
| Manual selection | Rectangle/ellipse, lasso/polygon, brush/pixel selection and combination operations. | Make temporary selection, layer mask and image content unmistakable targets. Verify modifiers, cancellation, transforms and selection-to-layer/effect operations. Qualify or add contiguous wand selection and selection-border transforms; no standalone wand implementation was found in the inspected paths. |
| Smart selection | Object/subject and generated-mask infrastructure, prompts, hover/candidates, asynchronous workers and stale-result guards. | Test model/provider availability, click-to-result latency, thin structures and ambiguous objects. Reuse source features; refine the chosen region at source resolution. |
| Brush | Mask painting with size, hardness, flow, spacing, smoothing, pressure responses, tablet input, erasing, presets, stroke replay and asynchronous raster commits. | Fix synchronous live-preview work. Add a true photo color-paint target with transparent raster layers, shared dab semantics, selectable colors and tile history. Do not confuse Pixel Brush selection with RGBA painting. |
| Eraser | Mask subtraction, tablet eraser input, linking selections to layer masks, and background removal through subject-mask refinement. | Preserve the useful nondestructive mask eraser; also implement ordinary pixel-alpha erasing, clear target feedback, sample-based background erasing, protect-color/tolerance controls and selection clipping. |
| Retouch / inpaint | Remove, Heal, Clone and Patch commands; source-region patching; LaMa ONNX inference with optional CoreML acceleration and CPU provider; local diffusion/exemplar fallback; candidates, blend and retry/finalization state. | Qualify texture/structure preservation, context sizing, source sampling, seams, large regions, cancellation and high-resolution export. A prompt field does not establish semantic generative fill. |
| Text | Canvas paragraph creation/edit drafts, transactional Done/Cancel, font families/faces, fractional stored sizes, weight/italic/underline, tracking, line height, baseline offset, wrapping, transforms and RGBA64 layout rasterization. | Fix draft/final appearance mismatch. Add point text, mixed style ranges, stronger paragraph controls, missing-font handling and shared caret/layout metrics. Prove native IME and actual save/relaunch/export behavior. |
| Filters | Typed ordered photo effects, codecs, layer-content/composite-below scopes, effect masks, amount, enable/reset/duplicate/reorder/remove, search picker and temporary preview Apply/Cancel. Looks/LUTs also exist separately. | Only four registered photo operators: Gaussian blur, box blur, linear-RGB invert and linear-RGB posterize. Extend this stack; add presets, previews and common creative effects after end-to-end qualification. |
| Blurs | Gaussian and box RGBA effects with document scaling, edge modes, cancellation, alpha-aware processing and padded tiles. | Qualify large-radius speed and masked boundaries. Add motion, radial/zoom and edge-preserving blur, then field/iris/tilt-shift and depth-aware lens blur with canvas controls. |
| Layer foundation | Image/pixel/fill/text/adjustment content, transforms, masks, blend/opacity, source linking and persistence infrastructure. | Current working tree now has nested parent identities, group drag/drop and collapse in the layer panel, plus recursive child compositing with group opacity and blend modes. `editor_preview_math_test`, `editor_preview_photo_layer_group_test`, and the live `desktop_shell_layer_hover_test` pass. Group-level mask/effect combinations and export parity still need focused proof before claiming the whole hierarchy workflow complete. |

Primary entry points: [StudioWorkspace.qml](../apps/desktop/qml/workspaces/StudioWorkspace.qml), [MaskCanvasOverlay.qml](../apps/desktop/qml/workspaces/MaskCanvasOverlay.qml), [SharedMaskOverlay.qml](../apps/desktop/qml/workspaces/SharedMaskOverlay.qml), [editor_preview_controller.cpp](../apps/desktop/src/editor_preview_controller.cpp), [mask schema](../apps/desktop/src/edit_recipe_mask_schema.h), [AI runtime](../apps/desktop/src/ai_model_runtime.cpp), [PhotoEffectStack.qml](../apps/desktop/qml/components/PhotoEffectStack.qml), [effect registry](../engine/include/photo_editor/pipeline/photo_effect_registry.h), [layer compositor](../engine/src/pipeline/color_layer_runtime.cpp).

## Specific findings to act on

### 1. Live brush work remains synchronous and scales with the preview area

`addBrushPointWithTabletEraser()` calls `updateActiveBrushLivePreview()` directly during an active stroke. The latter allocates a preview-sized alpha array, rasterizes samples and scans the whole preview to create colored feedback. Auto-mask also calls `decodeSourceImage()` there; a cache may reduce its cost, but this is still a synchronous dependency. The ordinary shared-raster path returns before the later `brush_preview_compositor_` path, so the presence of the Metal compositor does not prove that normal live strokes use it.

`processNextPendingBrushCommit()` already coalesces compatible work and sends raster commits through `QtConcurrent`. Preserve that architecture. First optimize the actual live path: persistent buffers, changed rectangles, bounded source patches, source-feature caches and incremental display uploads. Keep the shared raster semantics that existing tests protect. Profile history snapshots and publication separately before changing them.

Sources: `editor_preview_controller.cpp` functions named above; [brush_rasterizer.cpp](../apps/desktop/src/brush_rasterizer.cpp), [brush_stroke_sampling.cpp](../apps/desktop/src/brush_stroke_sampling.cpp), [brush_matte_rebuild.cpp](../apps/desktop/src/brush_matte_rebuild.cpp).

### 2. Text editing and committed rendering use different presentation contracts

[PhotoTextOverlay.qml](../apps/desktop/qml/components/PhotoTextOverlay.qml) uses a plain `TextEdit`, forces `color: "#ffffff"`, and does not apply saved `line_height` or `baseline_offset`. [photo_text_layout.h](../apps/desktop/src/photo_text_layout.h) uses the saved color and those metrics. The style model supports fractional sizes while the overlay uses `font.pixelSize`, another boundary to qualify. This is a confirmed mismatch in configuration, not a claim that all text exports are broken.

Use a shared layout/style contract for draft text, caret/selection, committed preview and export. A white accessibility outline can be separate from the actual text fill. Add image comparisons for colored multiline text with nondefault line spacing, baseline, fractional size and rotation; keep native keyboard/IME checks separate from raster tests.

### 3. Inpainting is removal/fill infrastructure, not general prompt-based generation

`model_input_size_for_patch()` returns 512×512. `runInpaintSession()` sends image and mask tensors, not text conditioning, to LaMa. Source/context patches pass through ARGB32 and resizing. The CPU fallback uses prompt keywords for texture heuristics. `run_ai_inpaint()` can deliberately select CPU filling for spots or fall back when the model fails.

Make the actual mode and degraded state clear. Improve context selection and source-resolution detail reconstruction, using tiled/overlapping processing only after seam and structure tests. Keep model input precision requirements explicit while preserving the surrounding working-space image and final blend precision. Test cancellation during inference: the inspected ONNX call uses a fresh `Ort::RunOptions`, so discarding a stale result alone does not demonstrate that compute stops.

Semantic prompt-driven generation is a separate optional model project with hardware, model-size and distribution requirements. It must not be implied by the existing prompt control or by renaming LaMa.

### 4. Blur has a working reference implementation but an expensive large-radius path

[photo_effects.cpp](../engine/src/pipeline/photo_effects.cpp) implements Gaussian blur as separable convolution with a kernel loop per pixel. Box blur already uses a sliding window. Tiled effect execution exists, but tiles still copy padded content and retain a full output buffer; active layer effects also use an output copy for atomic publication. This is not a fully sparse, tile-resident document pipeline.

Measure radius, stack depth, padding and peak memory separately. Cache unaffected intermediate results and reuse scratch buffers. Add accelerated kernels and an appropriate large-radius strategy, retaining the current CPU implementation as a reference. Any approximation must be versioned or confined to a clearly managed interactive-quality tier; final output must not silently change with the backend.

### 5. Existing proof is stronger for contracts than for the complete experience

The mask performance proof script verifies source markers/checklist items and focused tests. That is useful regression protection but not a substitute for pointer-to-present latency distributions on real photographs. The older checklist's completed async/dirty-region/cache items can coexist with synchronous live work elsewhere. Keep their receipts and add current workflow evidence rather than interpreting old checkmarks as universal readiness.

## Delivery sequence

Each phase ends in a usable, reversible workflow. Do not start by rewriting the entire controller or replacing every renderer. Extract focused services only where needed to enforce the contracts below.

| Phase | Implementation scope | Exit gate |
| --- | --- | --- |
| 0 — Reproduce and baseline | Build a failure matrix per tool; record native pointer/keyboard workflows and stage timings on JPEG, alpha PNG and real RAW files. Capture target identity, image size, backend and cold/warm state. | Reproducible cases, visual evidence and timing distributions for all requested tool families. Unreproduced reports remain open. |
| 1 — Interaction and correctness | Repair live brush allocations/scans, text draft fidelity, input ownership and cancellation; enforce document/layer/mask identity through async completion. Extend existing caches/dirty regions and keep expensive serialization/thumbnails outside drag events. | Brush/erase/text/selection remain responsive while rendering; no stale publication, target leakage or missing final stroke; one logical undo per gesture. |
| 2 — Masks and selections | Unify canonical alpha sampling and transformed geometry; accurate source-resolution refine; wand/contiguous selection if still absent; selection transform, selection-to-mask/effect/copy, useful inspection modes. | Clean cutouts on the quality corpus; exact add/subtract/intersect/invert behavior; no drift through crop, pan, zoom, rotation or export. |
| 3 — Color painting and erasing | Shared stroke engine with explicit mask-alpha, paint-RGBA and erase-alpha targets; transparent paint layers; colors, opacity/flow, blending, pressure, tip shape, spacing and smoothing; tile deltas for history. | Designer can paint, partially erase, repaint, constrain by a selection and save/reopen/export without flattening or altering the source. |
| 4 — Retouch and inpainting | Source overlay and aligned clone; deliberate sample scope; remove/heal/patch specialization; context and seam improvements; high-resolution detail; real model/provider tests; progress, cancellation and retry. | Convincing small and large removal on real media, preserved pixels outside the accepted mask, consistent preview/export, safe cancellation and retained edits when inference fails. |
| 5 — Filters and blur | Qualify the four current operators and Apply/Cancel first. Optimize Gaussian; add motion blur, high pass/unsharp mask, grain/noise, pixelate and threshold; then radial/zoom and edge-preserving blur. Add presets, recent/favorite effects and image previews. | Editable masked stacks survive reorder, undo, relaunch and export; tiled/full-frame and backend agreement; interactive performance across agreed radii and stack depths. |
| 6 — Typography and designer finishing | Point and paragraph text, style ranges, paragraph spacing/alignment, reliable fonts and native IME; outline/shadow/glow; real group compositing for group-level operations. | A poster/product graphic remains editable with faithful text, edges, styles and grouping after reopen and full-resolution export. |
| 7 — Advanced tools and release qualification | Field/iris/tilt-shift/depth lens blur, specialized brushes, pen/path masking and other advanced workflows after the core gates. Run long sessions, failure recovery, packaging and supported-platform verification. | Publish an explicit supported capability matrix backed by current evidence; leave advanced unsupported capabilities visible in the roadmap. |

Dependencies: phases 2–6 rely on phase 1's target, history and render contracts. Mask refinement underpins erasing, retouch and effect masks. Paint/erase share one stroke engine. Text style effects reuse the effect infrastructure. Group effects depend on actual group compositing. Advanced blur depends on the basic blur quality/performance gates.

## Shared technical contracts

1. **Coordinates:** document pixels define stroke sizes, feather distances, blur radii, text geometry and placement. Reuse the coordinate mapper for view/document/layer transforms. Preview scale changes sampling, not the edit. Include high DPI, nonuniform transforms, image orientation, crop and rotation.
2. **Targets:** every gesture captures document, layer/content, mask/effect and revision identity. Erase on a mask restores/hides coverage; erase on paint changes pixel alpha. Locked, hidden and unsupported targets have predictable behavior and actionable feedback.
3. **Transactions:** Begin/Update/Commit/Cancel semantics across tools. Escape restores the pre-gesture state, Apply/Done creates one command, a rejected commit retains the draft, and tool/document switches cannot publish into another target. Reuse existing transaction mechanisms.
4. **Rendering:** propagate changed bounds plus filter halos through mask, content, compositing and texture upload. Cache by content/transform/effect/color-profile/model revision and quality level. Cancel superseded work without starving the final result. Never use simplified outlines as final alpha.
5. **Storage/history:** retain immutable stroke/effect/text definitions and content-addressed assets; add raster tile deltas/checkpoints for paint rather than full-image snapshots per pointer event. Bound cache/history memory and provide recoverable storage failure behavior. Preserve existing recipes through versioned adapters; no catalog reset is needed.
6. **Color and alpha:** define straight/premultiplied alpha at each boundary, blend in the intended working space and avoid dark fringes on transparent blur/paint/text. Specify CPU/GPU tolerance and HDR behavior. A model's 8-bit input must not implicitly quantize the entire image.
7. **UI:** a clear active target, compact contextual settings, predictable keyboard shortcuts, adjustable cursor and accessible controls. Keep advanced settings available without obscuring the photograph. Show progress/failure/cancellation for long work and accurate visual feedback for fast work.

## Performance qualification

These are proposed product targets, not measurements from this audit or claims about Photoshop. Phase 0 must name the reference machine, display refresh/DPI, power state, build configuration and backend before locking budgets. Record p50/p95/p99, maximum stall, queue depth and peak memory; do not average away long pauses.

| Operation | Initial target on the agreed reference machine | Measurement boundary |
| --- | --- | --- |
| Pointer handler | p95 ≤ 4 ms | Input dispatch through return, excluding asynchronous work |
| Brush/erase/cursor visible response | p95 ≤ 33 ms; steady 60 Hz interaction target | Input timestamp to frame presentation with the corresponding feedback |
| Small local stroke settle | p95 ≤ 150 ms for a stroke affecting ≤ 1% of a 24 MP image, warm source | Pointer release to correct committed viewport result; full export timed separately |
| Selection geometry/outline feedback | p95 ≤ 50 ms during manual interaction | Input to presented geometry; final alpha qualification separate |
| Text typing/caret | p95 ≤ 33 ms | Key/composition update to presented text/caret |
| Effect parameter feedback | p95 ≤ 100 ms at 2560-pixel preview edge | Parameter event to correct visible draft; profile sigma 1/10/50/100 px and stacks of 1/5/10 effects separately |
| Smart selection / inpaint | Progress visible ≤ 100 ms; UI cancellation acknowledgement ≤ 100 ms | Report cold/warm model load, inference, refinement, transfer and publication separately; set completion budgets from the real baseline |
| Long session | Bounded caches/history; no monotonic retained-memory growth after eviction | Repeated edit/undo/document-switch cycles with queued work drained |

Exercise 24 MP and 45–60 MP files, 4K/Retina display, fit/100%/400% zoom, 1/10/50 masks or layers, short and long strokes, large-radius blur and concurrent export. Use supported Metal and CPU paths separately; add Windows runtime results before claiming equivalent Windows capability.

Extend [interaction_workflow_benchmark_driver.cpp](../tests/interaction_workflow_benchmark_driver.cpp), existing actual-app workflow instrumentation and [run-mask-performance-proof.py](../scripts/perf/run-mask-performance-proof.py). Collect event → queue → worker → delivery → upload → frame-present timings. A worker completion timestamp is not visible latency. Reject missing samples, skipped hardware checks and fake-model runs as performance/quality passes.

## Acceptance by tool

| Area | Required proof before calling it professional |
| --- | --- |
| Masks/selections | Reference alpha fixtures and real cutouts for hair/fur, fine wires, fabric, glass, soft shadows and small holes; boundary error, soft-alpha error and visual review on light/dark/replacement backgrounds. Identical canonical geometry through preview, reopen and export. |
| Brush/eraser | Fast diagonals, slow curves, overlapping dabs, repeated erase/restore, pressure/tilt and device eraser, exact hard pixels and soft edges. Compare live/committed/export coverage; no gaps, opacity jumps or destructive changes to the wrong target. Test actual supported tablets. |
| Retouch/inpaint | Skin, sky gradients, repeated bricks/fabric, object edges and large distractions. Compare detail/structure and seams; outside-mask difference must be zero except declared blend support. Test real installed model, absent model, CPU fallback, cancel, retry, undo and export. Human review is necessary; a numerical score alone is insufficient. |
| Text | Colored multiline paragraphs, fractional sizes, tracking/leading/baseline, rotation, overflow, missing fonts, Arabic/RTL, CJK, combining characters and emoji. Native caret/selection/IME plus actual export-worker and relaunch tests. |
| Effects/blurs | Impulses, ramps, transparency, cutout edges, HDR highlights and real photos. Radius scales correctly; full-frame/tiled output agrees; no seams or dark halos. Check masked-stack order, disabled effects, rejected parameters, Apply/Cancel and serialized reopen. |
| All tools | Consistent target routing, lock behavior, shortcuts, Done/Cancel, one-step gesture undo, redo, failure recovery, save/relaunch/export and a long mixed-tool native session. Screenshots/recordings accompany timing and numerical evidence. |

Keep three evidence classes distinct: algorithm tests, integrated controller/persistence/export tests, and native visual/performance/hardware tests. All three are required for final acceptance. Record fixture hashes, model/provider, recipe, build revision plus dirty diff, platform and raw timing samples with each receipt.

## First implementation batch

1. Capture a native 24 MP RAW brush/auto-mask/erase trace, including frame presentation; reproduce colored multiline text draft/final disagreement. Preserve before artifacts.
2. Make live brush feedback incremental without changing raster semantics. Reuse buffers and changed bounds; remove source decoding from the pointer handler through prepared source patches. Prove full strokes remain visible through delayed commits.
3. Make text drafts honor stored color and layout metrics with shared layout geometry; add targeted rendered comparisons and native editing evidence.
4. Test rapid tool/target/document changes, release/cancel, erase direction and pending worker completion. Fix observed failures in the existing transaction/identity paths.
5. Compare before/after visible latency, correctness and memory; perform save/relaunch/export on those repaired workflows. Only then start the larger color-paint and effect additions.

Re-estimate implementation effort after this batch. The current tests passing is evidence to preserve working foundations, not justification to estimate the remaining work as cosmetic UI polish.

## Scope benchmark

Use workflow behavior as the comparison, without promising complete Photoshop parity. Adobe documents editable/masked [Smart Filters](https://helpx.adobe.com/photoshop/using/applying-smart-filters.html), [selection-edge refinement](https://helpx.adobe.com/photoshop/desktop/make-selections/refine-modify-selections/refine-and-soften-selection-edges.html), distinct [painting tools](https://helpx.adobe.com/photoshop/using/painting-tools.html), [point and paragraph text](https://helpx.adobe.com/photoshop/desktop/text-typography/get-started-with-text/add-text.html), and on-image [Blur Gallery controls](https://helpx.adobe.com/photoshop/using/blur-gallery.html). These references inform the requested capability scope; the proposed performance budgets are ours.

Blank documents, portable projects, broader vector design, PSD/PSB interchange, CMYK/print workflows and semantic generative fill require separate explicit acceptance scopes. They should be planned alongside this program if the product is to become a general Photoshop replacement; completing the tools above alone would not prove that broader claim.
