# Creative tools: direction, delivery plan, and completion metrics

Date: 2026-09-25. Scope: photo filters, typography, brushes, and matching agent tools.

The direction is a contemporary photographer's darkroom: precise images with a few tactile, slightly strange interventions. Every tool should begin quiet, respond to image content, and make a finished frame feel authored rather than processed. Avoid one-click preset looks, generic dirt overlays, loud default chromatic aberration, and effects that compete with the subject. Texture, light, and type should inherit context from the photograph, selected fragment, or drawn path. The family names below are working names for this visual language; they describe a point of view, not a stock-filter catalog. The wider brainstorm remains optional future scope.

This is a supporting specification under [capability verification](capability-verification-plan.md). Foundation implementation status stays with the [mask/layer/selection owner](mask-layer-selection-checklist.md); performance receipts stay with [performance measurement](performance-measurement-expansion-checklist.md). The [professional tools plan](professional-editing-tools-upgrade-plan.md) supplies the wider engineering contracts. This document defines targets, not claims of completed implementation.

## Current foundation and actual gaps

- Existing text controls include fonts, weight, spacing, point/paragraph layout, color, stroke, offset shadow, transforms, and perspective. New typography work concerns variable-font axes, contour layout, and material rendering.
- The layer-effect registry now contains nine operators: Gaussian blur, box blur, motion blur, unsharp mask, posterize, invert, Optical Diffusion, Imperfect Print, and Dragged Shutter. The production photo.edit_composition command exposes typed add_effect for all nine and now also supports update_effect, remove_effect, reorder_effect, mask_effect, and clear_effect_mask using the same undoable layer-effect stack as the editor. Updates retain stable effect IDs; invalid fields and unsupported values fail without mutation. A controller-backed MCP integration test covers editing, order, selection masks, removal, and Undo restoration. See the [MCP effect workflow receipt](evidence/creative-tools/agent-live-workflow/README.md#typed-photo-layer-effects-over-mcp). Reuse existing color/render primitives where appropriate and check other pipelines before duplicating an operator.
- The production MCP composition tools now let an agent inspect a chosen font face's actual OpenType axis tags and ranges, create an editable transparent paint layer and receive its stable ID, add seeded typed layer effects, then target that layer with a brush stroke. This closes two A2 workflow prerequisites; model-driven print-cover execution and visual acceptance remain outstanding. See the [agent parity workflow receipt](evidence/creative-tools/agent-live-workflow/README.md#agent-font-axis-and-paint-layer-discovery).
- Top-first ordering, subject cutouts, text behind a subject, and composition tools have [focused evidence](evidence/photo-composition-2026-09-24/README.md). The mask-row menu distinguishes linked copies from asynchronous independent snapshots. Base-photo snapshots bake current development and soft selection alpha into pixel tiles; content-layer snapshots rasterize the layer and intersect its direct mask, linked selections, and active selection into self-contained pixel tiles. The user menu and agent expose both copy modes. Native full-workflow qualification is still open.
- Selection routing, lasso detail, pixel erasing, cutout materialization, and switching gestures have [repair evidence](evidence/selection-repair-2026-09-24/README.md). The production QML path passes three fresh-process 100-cycle brush/click-away/reselect/lasso stress runs with exact per-cycle baseline restoration, and the actual Cocoa desktop path now passes three more fresh-process 100-cycle runs. The native regression exposed a missing `controlsChanged()` notification when a brush child mask was added; the controller now refreshes the layer rows while keeping the lightweight render path. The native runs use the test segmentation provider, so they establish pointer/UI switching stability on this Mac, not real segmentation accuracy. Native crash reproduction and tablet behavior remain open. A real Apple Vision run on the pinned Obama portrait failed to initialize the system ANE model, so the installed subject/background segmentation gate is explicitly **failed on this host**, not passed by the deterministic provider or the error-tolerant smoke CTest. The local multi-prompt Smart Select provider previously returned just 168 selected pixels from a 2687x3356 real portrait; that sparse-mask failure motivated a prompt-coverage and fragment guard: explicit positive prompts must land in a component larger than a lone speck, while brush paths may select multiple subjects. A checksum-pinned Apache-2.0 SAM2 Hiera Tiny encoder/decoder now runs through ONNX Runtime and the guided Smart Select user and evidence-guided agent paths. The current pinned 12-photo corpus diagnostic produces full-resolution masks; all prompt samples pass polarity checks and the measured positive samples are connected. Coverage at alpha 96 ranges from 0.87% to 65.14%, with inference taking 2.19-9.48 seconds. A per-candidate alpha calibration now keeps all explicit positive and negative points on their required sides of the alpha-96 threshold. With revised prompts inside the snowfield, the coarse panorama diagnostic now passes at 80.45% snow-target coverage and 0.0085% excluded-sky coverage. This clears only that annotated slice, not full-image segmentation or human photo review. The preview path now feeds prior decoder logits to SAM2, keeps the last preview when refinement fails, and commits a matching preview without rerunning inference. The controller now resizes low-resolution instance previews to decoded dimensions, and sparse drags validate explicit user points while permitting multiple selected subjects. The controller regression passes end to end with the test segmentation provider; the separate SAM2 corpus run now passes the coarse panorama ROI but does not establish full-photo segmentation fidelity. See the [real-segmentation diagnostic](evidence/creative-tools/real-segmentation/README.md), its [initial panorama failure overlay](evidence/creative-tools/real-segmentation/sam2-corpus/landscape-mountain-panorama-overlay.jpg), and the [calibrated snow selection](evidence/creative-tools/real-segmentation/sam2-corpus/landscape-mountain-panorama-calibrated-overlay.jpg), [region annotation](evidence/creative-tools/real-segmentation/panorama-region-annotation.json), [region evaluator](../scripts/perf/evaluate-smart-select-regions.py), [repeatable corpus diagnostic](../scripts/perf/diagnose-sam2-corpus.py), the [machine-readable results](evidence/creative-tools/real-segmentation/sam2-corpus/sam2-corpus-diagnostic.json), and the retained [SAM2 mask](evidence/creative-tools/real-segmentation/sam2-obama-mask.png). Multi-photo mask fidelity, edge quality, and the human visual-review gate remain open. Vision errors still direct users to Lasso or Brush Add/Remove while preserving the model diagnostic; automatic semantic subject inference remains unresolved.
- CPU mask-brush preview rasterization now runs on a low-priority worker while QML receives stroke points immediately; stale worker frames are rejected after target or generation changes. Local-adjustment edits now use draft-first scheduling with a pending max-quality render, and draft publication preserves a higher-resolution native viewport texture. Full-fit settled renders can share preview-cache entries when scopes are hidden; scope-visible requests retain distinct cache identities so high-precision histograms do not derive from byte-cached output. The focused rasterizer, live-brush, two-cycle production-QML selection/lasso, active-viewport draft, and source-backed outline-trace CTests now pass together (5/5). A dropped viewport-local color-mixer drag flag caused QML reference errors and interrupted brush release in the lasso smoke; restoring it clears both failures. One fresh 100-cycle production-QML switching stress run also passes after the worker change. Three fresh native Cocoa 100-cycle sessions now also pass after the worker change; the three 60-minute mixed-tool stability sessions remain open. The release actual-QML diagnostic still misses its warm frame budget (five controller-injected events, p95 65.8 ms). A separate offscreen paint diagnostic on a 24 MP synthetic fixture measured first-frame p50/max 15.28/15.35 ms across only three trials; its `native_texture_item=false` and `photographic_fixture=false` flags exclude it from physical-input qualification. See the [live-brush feedback receipt](evidence/creative-tools/live-brush-feedback/README.md). Brush interaction performance remains open.
- F1, F2, F3, T1, and B1 have implementation receipts for in-progress vertical slices. Baseline completion for the nine feature families below remains **0/9 qualified**; existing foundation tests and partial receipts do not increase that count.

## Nine feature families

| ID | Feature and minimum editable controls | Distinctive result and feature-specific acceptance |
| --- | --- | --- |
| F1 | **Halation Study** — highlight threshold, knee, radius, strength, warmth, highlight protection | Bloom follows bright detail like a lens response, with a warm/cool bias available for deliberate color separation. Begin with small, image-dependent halos; keep shadows and skin texture intact. On a black/highlight chart, pixels beyond declared kernel support remain unchanged and strength zero is identity. No hard rings, clipped highlights, or whole-image haze. |
| F2 | **Misprint Atelier** — paper character, ink spread/density, per-plate registration, seed | Build a restrained cover or contact sheet from the photograph itself: color plates slip differently, ink gathers at edges, and paper grain stays nearly invisible until inspected. Registration stays in document space; synthetic edges move by the requested offset within 0.5 source pixel. No tiled seams or generic dirt overlay. |
| F3 | **Flash-Drag** — trail length, editable direction/path, falloff, highlight weighting, protected subject mask | Hold a portrait or object crisp while ambient light pulls into an asymmetric trail, like a deliberate slow shutter with a late flash. The protected subject stays clean without a dark cutout fringe. Must add nonuniform path/falloff behavior beyond ordinary motion blur. |
| T1 | **Editorial Width** — supported font axes, range styling, spacing, alignment | Make a headline fit the photograph's negative space through real font design axes and careful spacing, not fake horizontal stretching. Verify three variable fonts plus a missing-font case; expose supported ranges and deterministic static-font fallback without mislabeling it as an axis. |
| T2 | **Silhouette Lettering** — editable baseline, mask-to-path, offset, side, start/end, spacing; paragraph exclusion | Let a title trace a shoulder, horizon, building edge, or selected shape, then detach it so later selection edits do not break the type. Preserve curves and holes, show overflow, and keep analytic baseline deviation at or below 0.5 source pixel. |
| T3 | **Glass Letterforms** — frosted glass, smoked plastic, soft emboss; blur/transmission, tint, edge softness, lighting/depth | Treat a few words like a physical object in the frame: the photo bends through glass, darkens under smoked material, or catches a narrow embossed rim. Backdrop sampling reads only the lower stack; no self-sampling cycle. All three looks remain legible at normal size and fully text-editable. |
| B1 | **Drypoint** — tip texture, paper interaction, spacing, flow, angle, pressure response, seed | Draw marks that feel pressed into a chosen stock: graphite catches paper tooth while dry ink breaks along the hand's travel. The two materials must look and behave distinctly, replay deterministically, and avoid visible dab repetition or gaps. Zoom must not change document-space geometry or grain scale. |
| B2 | **Afterglow** — core width, falloff, color, exposure, taper, pressure, accumulation | Paint a narrow luminous core with a soft, controllable afterimage that can ride a reflection, window, or stage light. Preserve finite HDR values and explicit overlap behavior; no boxy bounds, clipped tails, or preview/export shifts. |
| B3 | **Photo Fragments** — capture source, alpha, tip bounds, spacing, rotation/scale variation, seed | Turn a selected feather, flower, reflection, or other photo detail into a reusable mark that can trail, scatter, or echo along a gesture. The captured tip is self-contained and survives source edits/removal, reopen, and preset transfer. Preserve soft alpha and transparent holes; variation must feel composed, not random noise. |

All numerical targets below are proposed acceptance thresholds. Record actual measurements separately; do not adjust thresholds after seeing results without documenting the reason and revised scope.

## Delivery order

| Phase | Work | Exit evidence |
| --- | --- | --- |
| 0 — Qualify the foundation | Native selection/brush/lasso switching; explicit linked versus independent selection copy; erase; top-first effects; stable agent targeting; fixture and performance baselines. | Foundation gates below pass. A native recording shows subject selection → brush refinement → independent cutout → text behind subject → erase → reorder → Undo/Redo → save/relaunch/export. |
| 1 — Establish the visual language | F1 Halation Study, B1 Drypoint, T1 Editorial Width. Implement UI and agent controls with each feature. | Three families pass all shared gates; create three editable photographic compositions using these tools, with subtle and expressive variants. Review visual direction before generating a larger preset library. |
| 2 — Build signature compositions | F2 Misprint Atelier, T2 Silhouette Lettering, B3 Photo Fragments. | Six families qualified cumulatively. Complete an editorial portrait, a print-style cover, and a photographic collage through both manual and agent routes. |
| 3 — Add optical/material depth | F3 Flash-Drag, T3 Glass Letterforms, B2 Afterglow. | Nine families qualified cumulatively. Verify backdrop dependencies, wide kernels, transparent edges, and HDR behavior in complete stacks. |
| 4 — Qualify the collection | Presets, discovery, keyboard interaction, mixed-feature stress, packaging, native walkthroughs. | All shared gates pass on the final build; 18 curated presets, six finished compositions, and published supported-backend/input matrix. |

Foundation fixes can accompany feature development, but a feature cannot be counted complete while a dependency fails. Deliver vertical slices: render/state model → native controls → agent operations → save/export → quality/performance evidence. Avoid finishing an entire engine-only batch before exposing real workflows.

## Shared completion gates

| Gate | Required measurement |
| --- | --- |
| Targeting and order | Pass at least 24 fixtures spanning above/below, opaque/translucent, masks, groups, locked/hidden targets, and transformed layers. Lower adjustments never modify the source or opaque appearance of higher content. Backdrop-dependent materials deliberately read the lower result. No mutation of an unintended target. |
| Selection reliability | Three native sessions, each with 100 brush → click away → reselect → lasso cycles, plus canceled/in-flight jobs and document changes. Zero crashes, lost accepted strokes, cross-document writes, or stuck gesture states. Test actual installed segmentation separately from deterministic stubs. |
| Pixel and mask correctness | Outside the defined affected region, working pixels remain exact on the same backend. For feathered erasing, verify alpha multiplication by inverse coverage within 1e-6. Validate holes, soft edges, source bounds, transforms, independent duplicate behavior, and mask refinement. Display outline maximum deviation at most 0.5 source pixel from its reference contour; never use the display outline as final alpha. |
| Editability and history | Every exposed parameter can be inspected, set, reset, and restored. Each gesture/command is one documented undoable operation; Undo/Redo restores canonical state exactly. Run a 100-operation mixed sequence and undo/redo it fully without drift. Presets preserve editable parameters and dependencies. |
| Persistence and output | For each family: save, close the app, reopen, edit again, export. Same-version CPU render round-trip is pixel-identical for deterministic seeded recipes. Preserve old recipes, fonts/fallback metadata, texture assets, and seeds. Missing assets produce explicit recoverable errors. |
| Render agreement | Compare settled full-quality preview and lossless export at the same dimensions and color transform. For SDR comparisons: 99.9% of channel samples differ by at most 1/255, maximum 3/255, alpha maximum 1/255. Compare HDR in linear space with absolute error <=1e-4 + 1e-4 times reference magnitude. Apply the same comparisons to CPU/Metal where supported, and inspect edge-difference images. Draft preview is measured separately. |
| UI access | Every feature has discoverable native controls, numeric entry, reset, preset save/load, keyboard focus, active-target feedback, and readable error state. Inspect at 100% and 200% UI scale and a 1280×800 window: zero clipped required controls or overlapping HUDs. New QML files are registered in the packaged module. |
| Agent parity | 100% of user-editable feature parameters and preset actions have typed agent access through the same controller paths. Inspect returns stable IDs and current values. Wrong/stale IDs, locked targets, malformed values, cancellation, and asynchronous failures are tested. Acceptance includes actual MCP dispatch, not just direct controller invocation. |
| Agent workflow quality | Six fixed creative briefs, three runs each on a recorded supported model/configuration: at least 17/18 complete without corrective user intervention and 18/18 avoid wrong-target edits or silent data loss. Retain prompts, tool traces, final recipes, and error states. Deterministic UI/agent replay of the same parameters must match exactly; natural-language variation is assessed visually. |
| Interaction performance | On the pinned reference configuration: pointer-to-first-visible brush feedback p95 <=33 ms and max <=100 ms; parameter-to-first-visible draft p95 <=100 ms; final settled preview p95 <=500 ms for the standard scene. No UI-thread block >100 ms from these tools. First-use timings are reported separately. |
| Heavy work and memory | A 45 MP stress document must remain cancellable, with visible cancellation response <=100 ms and no later stale commit. For 100 repeated apply/cancel cycles with unchanged final document/history, retained RSS growth after caches settle is <=64 MiB and has no continuing upward trend. Record peak RSS, history storage, export time, and scratch usage separately. |
| Stability | Three 60-minute native mixed-tool sessions on the final build: zero crashes, hangs longer than five seconds, lost accepted edits, or corrupt saves. No unresolved critical/high-severity defects in the delivered workflow. Missing telemetry or inaccessible hardware is unverified, never a pass. |

## Fixed agent-run briefs

Run each exact brief three times with the same recorded supported model and
configuration. Reset to the named source document before each run; supply the
current asset and mask IDs as fixture context, and judge returned IDs against
the IDs actually discovered or created. Retain the prompt, full MCP trace,
recipe, error state, export, and reviewer result for every attempt. No run may
flatten editable text or silently substitute a global adjustment for a local
composition operation.

| ID | Source fixture | Exact brief |
| --- | --- | --- |
| A1 — Editorial portrait | Obama portrait; prompted subject selection | “Create a restrained editorial portrait composition with the exact headline FLOAT across the upper torso. Use a large warm light serif with a dark fine stroke and subtle shadow. Put the person in front of the headline by independently copying the selected subject pixels above it. Use assetId composition and the existing maskId sam2-subject-selection. Return exactly two ordered photo.edit_composition calls: add_text with text FLOAT at x=950,y=1500, then copy_selection with that maskId and independent=true. Do not invent ids or make global image adjustments. Keep the text editable.” |
| A2 — Print cover | Licensed architecture image with a distinct facade edge | “Design a photographic print cover titled WEATHER / MEMORY. Fit the title with a real supported variable-font width axis; use restrained two-plate drift and paper/ink character with an explicit seed; add one drypoint annotation that catches the facade grain. Keep the photograph recognizable, preserve editable type and effect controls, and do not add generic dirt.” |
| A3 — Photographic collage | Licensed portrait or still-life with a clear selectable detail | “Build a small photographic collage from the supplied image. Capture the selected feather, petal, or reflection as a reusable photo-fragment tip, then place a short composed sequence of seeded marks on a separate layer. Preserve soft alpha and transparent holes, keep the source detail reusable after its source layer is hidden, and avoid random-looking scatter.” |
| A4 — Night/light study | Licensed night performance image with a visible moving-light direction | “Make a late-show frame with the exact words AFTER THE LAST SONG. Keep the performer crisp with the supplied protection mask, add a small warm halation around stage lights, pull ambient light into one directional flash-drag trail, and paint a narrow tapered afterglow along the existing reflection path. Keep the face free of halos and preserve editable effect, mask, and paint controls.” |
| A5 — Glass-lettered still life | Licensed glass product still life | “Set the exact small headline HELD IN LIGHT in the empty area beside the bottle. Render the letters as lightly frosted glass that samples only the image below the text layer. Protect the bottle label and highlights; keep the text editable, readable at fit view, and free of a bright outline or broad blur.” |
| A6 — Minimal monochrome typography | Licensed monochrome photograph with an existing closed selection | “Create a spare monochrome field-note image titled NORTH / 04. Use the supplied closed selection as a detached text baseline, respecting its holes and negative space. Keep the type editable and the photograph's monochrome tone unchanged; show text overflow rather than silently clipping it.” |

The 2026-09-26 production-bridge FLOAT runs use the exact A1 brief and complete
all three A1 attempts (**3/3 A1, 3/18 overall**). The other five briefs
have no qualifying attempts yet. The A1 runs are agent-dispatch evidence, not
visual approval; all scoring remains under the photographic review gate below.

Reference performance scene: a native release build on a recorded Apple Silicon Mac, 24 MP photograph, 2048-pixel-long-edge preview, ten layers, three masks, two text layers, and three active effects. Record exact hardware, OS, Qt version, backend, display scale, parameters, and document hash. Pin representative radius/tip/texture settings before measurement; separately test maximum settings rather than implying the standard-scene budget covers every extreme.

Collect at least 100 eligible events per latency metric in each of three fresh process runs; report per-run p50/p95/max and event counts. All runs must pass. Measure actual input receipt to presentation, not controller return or queued work. Use existing stricter repository budgets where they apply. CPU fallback gets its own measured report and supported limits; an unavailable Metal device cannot establish GPU parity. Physical pressure/tilt claims require a real tablet session.

## Visual acceptance: what tasteful means here

Use a fixed, licensed corpus of 12 real photographs: three portraits including difficult hair/skin, two night scenes, two architecture scenes, two product/still-life scenes including glass, two landscapes, and one monochrome photograph. Add synthetic alpha, gradient, impulse, and glyph charts for mathematical checks. Each family is reviewed on at least four relevant photographs, at low, default, and expressive strengths, at fit-to-window and 100% zoom.

Score each applicable dimension from 1–5: photographic integration, material/optical credibility, restraint at low strength, typographic readability, and freedom from artifacts. A 1 is unusable, 3 is visibly compromised, and 5 is ready for a finished composition. Require a mean >=4/5 for each family, no applicable dimension below 3 on any reviewed image, and zero visible seams, unintended halos, clipped glyphs, or unintended repeated stamp patterns. Use a second human reviewer where available; record reviewer identity and disagreements. Automated similarity scores cannot substitute for this judgment.

The fixed corpus can be reproduced with [the pinned corpus preparer](../scripts/perf/prepare-creative-tools-corpus.py), which downloads the twelve selected Commons originals at a paced rate and rejects byte changes against pinned SHA-256 values. The current local set passed `check-real-image-corpus.py --creative-tools`; the source/license receipt is [here](evidence/creative-tools/visual-review-corpus/README.md). The [creative-tools visual-review validator](../scripts/perf/validate-creative-tools-review.py) enforces the exact 12-photo category mix, verified-license metadata, four relevant photos per family, all low/default/expressive × fit/100% review cells, human reviewer identity, score floor/mean, and zero-artifact condition. It first invokes `check-real-image-corpus.py` to verify local bytes, license evidence, and hashes. The [review-packet guide](creative-tools-visual-review.md) defines the manifest annotations and packet format. Store the completed review packet alongside the corpus manifest; a schema-development run using `--skip-corpus-file-check` is never qualification evidence. The validator only checks completeness and arithmetic; reviewers still make and own the visual judgments.

Prepare one contact sheet for each feature family and six finished editable compositions: editorial portrait, print cover, photographic collage, night/light study, glass-lettered still life, and minimal monochrome typography. Record the user's visual-direction review after phase 1; until reviewed, call the art direction provisional rather than claiming taste is objectively proven. This is a product acceptance checkpoint, not a prerequisite for routine implementation or fixes.

### Visual-direction checkpoint

After reviewing the F1 Halation Study contact sheet across portrait, night,
architecture, and glass examples, the user responded **"On direction."** This
records preliminary acceptance of the overall visual direction. It does not
score F1's 24 photo/strength/zoom cells, qualify F1, or approve the other eight
families; the direction remains subject to the complete photographic review.

Use these same six briefs for the 18 model-driven agent runs. Each brief runs three times on the recorded supported model/configuration; retain each prompt, tool trace, recipe, render, and outcome. The run count measures task completion and safe execution, while the separate human review gate determines whether the visual result is tasteful.

| Brief | Required outcome |
| --- | --- |
| Editorial portrait (A1) | Set styled editable type behind a selected person using an independent foreground cutout; preserve the face and hair edge. |
| Print cover (A2) | Apply restrained plate misregistration and paper/ink character while keeping the photograph recognizable and seed-stable; fit the title with a supported font axis. |
| Photographic collage (A3) | Capture a selected source detail as a reusable photo fragment and place a controlled sequence of marks on a separate layer. |
| Night/light study (A4) | Combine restrained highlight glow, directional flash-drag, and a tapered painted light trail; preserve shadow detail and subject boundaries. |
| Glass-lettered still life (A5) | Add editable glass or smoked-material lettering that samples only lower layers and remains legible on the product. |
| Minimal monochrome typography (A6) | Place editable type along a detached image contour, respecting holes, spacing, and negative space without changing the monochrome image. |

Ship two curated presets per family (18 total): one restrained and one expressive. Each preset must work on at least three relevant photographs without undocumented adjustments. Presets are named parameter/asset recipes; they do not count as extra feature families.

## Evidence and progress accounting

For every feature, retain the source revision plus dirty-diff hash, build command, fixture hashes, full parameter/seed values, test logs, native recording/screenshots, saved recipe/assets, reopened export, difference images, performance JSON, and known limitations. Store receipts under `docs/evidence/creative-tools/<feature-id>/` when implementation begins. Do not create empty receipts that look like completed proof.

Track these fields in the canonical capability owner as work is delivered:

| Measure | Initial value | Completion target |
| --- | --- | --- |
| Verified creative review corpus | 12/12 | Exactly 12 licensed, hash-verified photos with the required category mix |
| Human visual-review cells | 0/216 minimum | 9/9 families × at least four relevant photos × three strengths × two zooms; all score gates pass |
| Fully qualified feature families | 0/9 | 9/9 |
| Shared gates satisfied per family | Unmeasured | All applicable gates; exclusions documented before qualification |
| Curated presets qualified | 0/18 | 18/18 |
| Finished compositions qualified | 0/6 | 6/6, editable after relaunch and faithful export |
| Native switching cycles / soak sessions | Not yet qualified to this plan | 300 cycles / three 60-minute sessions |
| End-to-end agent runs | 18/18 completed; A1–A6 each 3/3; human visual review pending | >=17/18 successful; 18/18 safe targeting and explicit outcomes |
| Blocking defects | Not yet triaged | 0 critical/high in scope |

Report completion as qualified families divided by nine, accompanied by the foundation and shared-gate results. Do not average a failed stability, rendering, or persistence gate away. A phase is complete only when its features and dependencies meet the gates; the collection is complete only after final native/package qualification. Feature status requires the receipts below and corresponding entries in the canonical capability owner.

### Implementation snapshot — 2026-09-25

F1 has an end-to-end vertical slice in the renderer, effect stack, app controls, recipe persistence, and agent recipe schema. See the [optical diffusion implementation receipt](evidence/creative-tools/optical-diffusion/README.md). T1 has a variable-axis style model, font-supported axis range discovery, live and committed rendering, typography controls, and agent typography/recipe support. Offscreen Qt Quick workflow evidence covers raster changes for three Mac variable-font families, an active axis control, missing-font fallback, undo, and version-4 recipe validation; see the [variable typography implementation receipt](evidence/creative-tools/variable-typography/README.md). T2 now has a contour-text renderer with detached mask baselines, native side/start/end/offset/spacing controls, on-canvas draggable baseline anchors with one-step undo/redo, visibly differentiated secondary contours and holes, paragraph exclusion across multiple closed contours, typed agent contour and flow controls, version-7 recipe persistence, and focused render/history/reopen tests including analytic circle/ellipse deviation measurements below 0.5 source pixel. Multi-photo visual review and shared qualification gates remain open. See the [contour typography implementation receipt](evidence/creative-tools/contour-typography/README.md). T3 now has version-8 editable material typography with frosted glass, smoked plastic, and soft emboss, native controls, lower-stack sampling, typed agent fields, recipe validation/reopen, and CPU composition tests for layer order and distinct material response. Photographic review and retained-GPU parity are open. See the [material lettering implementation receipt](evidence/creative-tools/material-lettering/README.md). B1 has direction-aware, seeded document-space Dry Ink and Graphite texture in the user pixel brush and agent composition command, with an offscreen Qt Quick capture and focused repeatability/undo tests; see the [dry ink and graphite implementation receipt](evidence/creative-tools/dry-ink-graphite/README.md). B2 now has an in-progress light-paint material in the same native/agent pixel-brush path, with extended-range radiance, bounded soft halo falloff, two-ended stroke taper, exposure, pressure, and overlap controls; full compositor/output qualification remains open. See the [light painting implementation receipt](evidence/creative-tools/light-painting/README.md). F2 now has a seeded document-space Imperfect Print vertical slice with red/blue plate registration, paper variation, ink controls, app/agent wiring, recipe persistence, and tile/determinism tests; see the [imperfect print implementation receipt](evidence/creative-tools/imperfect-print/README.md). F3 now has a directional trail with edge falloff and highlight weighting, effect-mask subject protection, app controls, persisted parameters, and typed agent recipe support; the registered MCP schema/validation and composition-handler contract also pass; see the [Dragged Shutter implementation receipt](evidence/creative-tools/dragged-shutter/README.md). B3 now has native Photo Fragments capture and save controls, a versioned self-contained PNG tip store, `.looksybrush` import/export, seeded stamping from layers or presets, and agent preset management. Focused tests verify transfer into a separate app-data directory, a fresh child process reload with an exact PNG hash match, source-layer removal, paint-tile recipe round-trip, and undo. A full app close/reopen editing walkthrough, photographic review, backend, performance, and model-driven agent qualification remain open; see the [Photo Fragments implementation receipt](evidence/creative-tools/photo-fragments/README.md). The human photographic gate now has an executable validator for its 12-image category contract and at least 216 scored review cells. The pinned [12-photo corpus receipt](evidence/creative-tools/visual-review-corpus/README.md) records a byte/hash/license-verified set, so corpus coverage is **12/12**; no human packet exists, so visual review remains **0/216**. Shared regression evidence now includes 48 top-first order fixtures across layer/group transforms, masks, visibility, opacity, and above/below adjustments; a locked-target rejection with unchanged state; a 100-operation alternating typography/transform history round-trip with exact recipe and pixel restoration; and a current 3/3 CPU focused CTest run covering renderer parity, spatial photo-effect behavior, and textured/preset paint paths ([shared render and paint regression receipt](evidence/creative-tools/shared-render-parity/README.md)). Three fresh-process 100-cycle selection-switch runs also pass under offscreen QML. Native selection-switch reliability is 3/3 sessions; real segmentation, native crash reproduction, tablet behavior, and the other shared gates remain open. Multi-image visual review, packaged-app walkthroughs, feature-by-feature output qualification, full CPU/Metal/output and performance qualification, model-driven agent runs with retained MCP traces, preset workflows, and remaining shared gates are outstanding. All nine families are in progress; **qualified families remain 0/9**.

### Review capture update — 2026-09-26

F1 now has a release-mode preliminary packet with four licensed full-resolution
photos at restrained, default, and expressive parameters, isolated effect and
baseline renders, difference maps, pixel-change measurements, saved recipes,
and exact reopened-export checks. The default variant uses the effect's reset
values, including zero highlight protection. A release regression now confirms
F1's current CPU route, exact output across recipe reopen, and explicit Metal
rejection for photo-layer stacks, even when device availability is assumed;
the current renderer does not accelerate any photo-layer stack. The registered
Agent MCP contract now covers F1 schema discovery, validation, reversible draft
application, and rollback. Human photo scoring, Metal parity, performance,
native UI sessions, and model-driven creative runs remain open. See the
[capture receipt](evidence/creative-tools/optical-diffusion/full-resolution-review-release/optical-diffusion-review-capture.json),
[review guide](evidence/creative-tools/optical-diffusion/full-resolution-review-release/README.md),
[F1 contact sheet](evidence/creative-tools/optical-diffusion/full-resolution-review-release/contact-sheet.html),
the [backend regression log](evidence/creative-tools/optical-diffusion/f1-backend-routing-ctest.log),
and [Agent MCP contract log](evidence/creative-tools/optical-diffusion/f1-agent-mcp-ctest.log).
Human visual review remains **0/216**, and F1 remains in progress.

### F1 review-matrix correction — 2026-10-01

The initial F1 contact sheet used an architecture image, which is outside F1's
relevant category set in the review validator. The canonical sheet now presents
the required portrait, night, glass-product, and landscape photos at all three
strengths. The dusk landscape uses a documented photo-specific highlight
threshold of 0.70 because the default 0.80 yielded no changed pixels there; all
three full-resolution renders passed the Qt workflow, save/reopen, and exact
export checks. The earlier architecture example remains supplemental and is
excluded from the F1 four-photo minimum. The [corrected contact sheet](evidence/creative-tools/optical-diffusion/full-resolution-review-release/contact-sheet.html)
and [capture receipt](evidence/creative-tools/optical-diffusion/full-resolution-review-release/optical-diffusion-review-capture.json)
are ready for fit and 100% inspection. These 12 effect outputs support 24 F1
photo/strength/zoom cells, but no scores have been entered; human review is
still **0/216** and F1 remains unqualified.

The [CPU fallback diagnostic](evidence/creative-tools/optical-diffusion/cpu-fallback-diagnostic/README.md)
adds three fresh release-process runs on a 19.96 MP photo. Each completed the
offscreen control, recipe-reopen, and exact-export path with pixel-identical
output; whole-process wall time was 96.73–99.21 seconds (median 97.11), with
4.21–4.38 GB peak resident set on an M4 Pro. This is a diagnostic for full-size
CPU render/export cost, not a settled-preview or interaction-latency sample:
the scene lacks the plan's 24 MP layered stack, and no p95 event series was
collected. CPU fallback performance qualification remains open.

### Foundation composition workflow recheck — 2026-09-26

The current release `photo_composition_workflow_test` passes with a retained
[CTest log](evidence/selection-repair-2026-09-24/photo-composition-workflow-release.log).
It covers top-first ordering through transformed groups, styled text occluded
by a selection cutout, agent copy/reorder/erase, independent base/content
cutouts, Undo/Redo, and exact recipe reopen. This is strong state/render
contract evidence, but it uses a synthetic rectangular mask and runs through
the controller test harness. It does not satisfy native person selection,
multi-image segmentation accuracy, outline quality, or native save/relaunch/
export; the foundation gate remains open.

An additional full-resolution controller diagnostic placed styled text behind
a copied SAM2 subject mask on the pinned 2687×3356 portrait and passed exact
serialized-recipe parse/re-render. The [review receipt](evidence/creative-tools/foundation-composition/real-subject-debug/README.md)
includes the before/after and face/headline detail images. A controller-driven,
single-stroke Brush Add refinement waits for the asynchronous raster worker and
changes 263,063 mask-alpha pixels. In the headline band, fully opaque selection
coverage over visible text rises from 0 to 50,486 of 65,056 pixels, and total
foreground-covered headline pixels rise from 43,809 to 56,529. The revised
render visibly places the headline behind the face. This remains one offscreen
portrait diagnostic: hair-edge quality, other subjects, native interaction,
relaunch/export, and human review are unqualified, so no plan counter increases.

### Live local-agent runtime check — 2026-09-26

The opt-in `agent_local_model_test` completed an image-grounded plan and proof
critique with the installed local `qwen3-vl:32b` model, with no cloud call,
fallback, or automatic commit. Planning took 86.7 seconds and critique 34.0
seconds. This verifies the local model service and image transport only; the
request used a global evening-look adjustment on a synthetic highlight fixture
and did not exercise creative-tool MCP calls. It contributes zero to the
required 18 model-driven creative runs and 216 visual-review cells. See the
[live runtime receipt](evidence/creative-tools/agent-live-workflow/README.md).

The focused release composition contract now also uses `AgentMcpClient` against
the bridge's live localhost MCP HTTP endpoint: it discovers the composition
tools, applies a run allowlist, and dispatches inspect plus typed text creation
over JSON-RPC. The tool schema now enumerates operations, exposes typed
composition value fields, and conditionally requires command inputs such as
`add_text`'s text and coordinates or `paint`'s points, with validated limits
for brush size, pressure, light exposure, and fragment variation. Its editor
handler is a stub, so this verifies schema discovery, transport, and operation
forwarding only; it is not a model-driven creative run and does not increase
the 18-run counter. See the [MCP dispatch receipt](evidence/creative-tools/agent-live-workflow/creative-mcp-dispatch.md)
and [release CTest log](evidence/creative-tools/agent-live-workflow/creative-mcp-dispatch-ctest.txt).

The release composition workflow now also drives the real `AgentMcpClient`
through a localhost JSON-RPC test endpoint into the production
`EditorPreviewController`: it creates editable text, styles that text, then
continues through the masked cutout, top-first rendering, history, and exact
recipe-reopen checks. This verifies controller-backed command execution from
the agent client, but the endpoint is a test adapter; the separate production
bridge contract still uses a stub editor handler. It does not prove the
production bridge-to-controller transaction or a model-selected call. The
retained [controller-backed MCP workflow receipt](evidence/creative-tools/agent-live-workflow/composition-controller-mcp.md)
and [release CTest log](evidence/creative-tools/agent-live-workflow/composition-controller-mcp-ctest.txt)
therefore add integration evidence without changing the model-driven run
counter. The separate production-bridge portrait run below is the first counted
run, bringing that metric to 1/18.

The Ollama planner now has a composition-specific routing rule: when the user
asks for text, typography, paint, masks, selections, cutouts, layer order, or
Photo Fragments and the matching MCP tools are available, it must return typed
tool calls rather than a global photo-tone edit intent. When exact asset and
mask/layer ids are supplied, it should use them directly and order dependent
operations together. The deterministic `agent_local_model_test` passes this
prompt contract. A live `qwen3-vl:32b` run then selected ordered `add_text` and
`copy_selection` operations and dispatched them through `AgentMcpClient` to
the real composition controller. A pixel comparison measured 848 headline
pixels occluded by the copied selection layer; recipe parse/reopen reproduced
the final render exactly. The retained source, selection, plan, calls, recipe,
and render are in the [synthetic live composition receipt](evidence/creative-tools/agent-live-workflow/README.md#live-local-agent-runtime-smoke)
and [run artifacts](evidence/creative-tools/agent-live-workflow/composition-run-synthetic-2026-09-26/run-receipt.json).
Because the fixture is synthetic and the MCP endpoint is a test adapter, this
does not count toward the required 18 model-driven creative runs, prove the
production bridge-to-controller connection, or advance the human review or
qualified-family metrics.

The production recipe planner now supports up to three sequential MCP tool-plan
rounds, with an eight-requested-call hard limit per draft and accumulated tool
results sent back to the model between rounds. This allows a dependent
composition plan to inspect a layer or create text, receive its returned id,
then request a follow-up operation using that id. The focused
`agent_model_mcp_tool_rounds_contract` CTest now passes through the real local
bridge and MCP client: a scripted Ollama response makes two successful
model-selected calls across two request-owned follow-up stages, compiles and
applies a reversible draft, and rolls it back. The fixture uses read-only live
status and recipe-schema calls, not a composition operation that consumes a
returned layer id. It proves production multi-round transport and continuation,
but not model-driven creative composition. Its retained
[CTest log](evidence/creative-tools/agent-live-workflow/model-tool-rounds-contract-ctest.txt)
and [workflow notes](evidence/creative-tools/agent-live-workflow/README.md#bounded-multi-round-mcp-planning)
document the boundary. The full bridge executable still fails later in its
unrelated batch-export receipt check, after this focused path has passed. This
read-only transport fixture does not advance family qualification, agent-run
count, or visual-review cells; the separate portrait workflow below advances
the agent-run count to 1/18 only.

### Production-bridge creative agent run — 2026-09-26

The first of 18 local model-driven creative runs completed on the pinned Obama
portrait and SAM2 selection. `qwen3-vl:32b` used the authenticated production
localhost MCP endpoint connected to the real `EditorPreviewController`: it
added editable `FLOAT` text, copied the selected subject independently above
it, then used the returned text-layer ID in a second model-planned typography
round. The receipt records 30,537 subject-affected headline pixels, 1,906
fully opaque subject pixels restoring the underlying photo over glyphs, and
pixel-identical recipe reopen. This is a workflow proof only; the typography
has not received human visual scoring and does not qualify a family or finished
composition. See the [production-bridge run receipt](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-26/run-receipt.json)
and [render](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-26/text-behind-subject.png).
Agent workflow progress at the time of this first-run note was **1/18**; family
qualification remained **0/9** and visual review **0/216**.

### Production-bridge A1 repeat — 2026-09-26

A second independent attempt used the exact A1 prompt, source portrait, and
SAM2 mask through the authenticated production bridge and local Qwen model. It
again dispatched editable text, independent foreground copy, and typed
typography through the real controller; the final recipe reopened to identical
pixels. It produced a separate attempt receipt and status journal, and the
runner now refuses to overwrite a prior run directory. Pixel evidence matched
the initial run (30,537 subject-affected headline pixels and 1,906 fully opaque
subject pixels over glyphs); recipe size was 168,351,671 bytes. Both A1 attempts
pass workflow checks, but neither has human visual approval. This brings agent
workflow progress to **2/18** (**2/3 A1**); family qualification remains
**0/9**, visual review **0/216**, and finished compositions **0/6**. See the
[second A1 receipt](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-26/A1-attempt-02-corrected/run-receipt.json)
and its [attempt journal](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-26/A1-attempt-02-corrected/run-status.json).

### Production-bridge A1 completion — 2026-09-26

The third independent A1 attempt also completed through the production bridge
with the local Qwen model and returned pixel-identical recipe reopen. Its
machine-readable receipt now records the source revision and hashes for the
scoped source files and working-tree diff. The model selected the same ordered
text, independent-cutout, and typography operations; the measured headline
occlusion remained 1,906 fully opaque subject pixels. This closes the three
workflow repetitions for A1, not the human visual review or the editorial
composition acceptance. Agent workflow progress is **3/18** (**3/3 A1**); A2–A6
remain unrun, family qualification remains **0/9**, visual review **0/216**, and
finished compositions **0/6**. See the
[third A1 receipt](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-26/A1-attempt-03/run-receipt.json)
and [attempt journal](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-26/A1-attempt-03/run-status.json).

### Production-bridge A2 print-cover runs — 2026-09-26

All three A2 workflow repetitions now complete on the licensed, hash-pinned
architecture photograph through the authenticated production MCP bridge and
local `qwen3-vl:32b`. The model created a linked source layer, an editable
`WEATHER / MEMORY` text layer, and a separate paint layer; it then selected the
installed Skia `wdth` axis, added seeded Imperfect Print with opposite red/blue
registration offsets and paper/ink controls, and painted a seeded three-point
Dry Ink annotation. Each receipt records all six successful tool operations,
the editable recipe, source/hash details, and a pixel-identical recipe reopen.
The first successful render changed 9,968,914 pixels overall and 2,356 pixels
from the annotation. Its visual inspection found the facade recognizable,
the headline legible against the dark lower facade, and plate drift restrained.
This is workflow evidence, not a human visual-review score or family
qualification.

The three qualifying receipts and renders are retained at
[A2 repeat 1](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-26/A2-two-plate-annotation-attempt-01c/run-receipt.json),
[A2 repeat 2](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-26/A2-three-point-stroke-attempt-02b/run-receipt.json),
and [A2 repeat 3](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-26/A2-three-point-stroke-attempt-03/run-receipt.json).
Earlier rejected attempts remain in separate directories and do not count.
Agent workflow progress is **8/18** (**3/3 A1, 3/3 A2, 2/3 A3**); qualified families
remain **0/9**, human visual review **0/216**, and finished compositions **0/6**.
The layer-creation and follow-up calls exposed an unconstrained `toolCalls`
array schema that allowed incomplete model output. Each tool-call item now has
a typed object schema requiring `name` and object `arguments`; its contract is
covered by `agent_local_model_test`.

Next is A3 Photo Fragments. The production composition tool now accepts a
normalized closed lasso path, creates one undoable editable selection, and
returns its stable mask-set ID; a focused controller-backed MCP test uses that
ID to duplicate selected pixels onto a transparent independent layer, checks
inside/outside alpha, and undoes both operations. This is selection/cutout
plumbing only. The A3 production runner now exercises the full dependent
sequence: model-selected lasso, independent copy, separate paint layer, saved
Photo Fragment preset, hiding the captured tip, seeded stamping, and exact
recipe reopen. That work exposed and fixed two MCP/controller handoff defects:
independent `copy_selection` now returns its stable `createdLayerId`, and preset
variation validation accepts integer JSON number types. Controller-backed
regressions pass for both. The retained A3 experiments have not completed the
full chain: one model invented an asset ID and was safely rejected, one runner
check incorrectly required six lasso points and was corrected to match the
three-point API minimum, one model used the correct layers but its integer
rotation setting was rejected before that validation fix, and one complete
sequence initially used normalized fractions for a pixel-space paint path,
placing the marks at the canvas origin. The runner now supplies dimensions and
target bounds and rejects that unit mismatch before hiding the source layer.
The A3 attempt completed all six production MCP operations, retained the named
preset, hid the cutout, stamped the separate paint layer, and reopened the
recipe pixel-identically. Its 17-point lasso improved the contour, but the
100% render still showed pasted-looking edges. Photo-fragment rotation and
scale now bilinearly sample premultiplied tip pixels, and lasso MCP accepts a
validated feather value from 0 to 0.05; the controller preserves small lasso
feather values and the copied-pixel regression verifies soft alpha survives
into the cutout. A second end-to-end follow-up used feather=0.005 and passed
recipe reopen, but its real-photo render still shows angular cloned fragments.
The [first A3 receipt and renders](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-26/A3-curved-rim-attempt-03/run-receipt.json)
and [feathered follow-up receipt and renders](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-26/A3-feathered-rim-followup-02/run-receipt.json)
retain both results. The third complete A3 run also passed all six MCP
operations, hid the selected tip, saved a reusable preset, and reopened its
recipe pixel-identically. Its paint path was a three-point zigzag across the
glass, leaving a conspicuous dotted diagonal. The previous mean-position guard
accepted that path, so it is replaced by ordered left-to-right curve anchors,
point-count and spacing bounds, and per-point distance checks before paint is
dispatched. The runner also rejects overbroad lasso plans before any MCP
mutation; one local-model response enclosed 1.73% of the image and was rejected
against the 0.8% maximum. All three A3 workflow-run slots are now exercised,
but A3 remains visually unqualified until the Photo Fragment reads as a
convincing glass-rim echo. A subsequent diagnostic run followed five validated
upper-left rim anchors and reopened its recipe exactly, but 100% review still
shows repeated triangular stamp edges. Its [receipt and renders](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-26/A3-curved-anchor-followup-04/run-receipt.json)
are retained beside the earlier [failed diagonal](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-26/A3-opposite-rim-followup-03/run-receipt.json).
Photo Fragment rendering now builds premultiplied, color-space-aware mip levels
and samples them with trilinear interpolation when a selected tip is reduced
substantially. A focused alternating-stripe regression catches downsampling
aliasing. The latest live A3 run used a five-point rim path with overlapping
low-opacity marks (spacing 0.12, opacity 0.42), retained 9,573 changed pixels,
and reopened pixel-identically; its [receipt and fit/100% renders](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-26/A3-overlap-marks-followup-05/run-receipt.json)
are preserved. This improves continuity, but it remains an individual
still-life diagnostic rather than the required four-photo human review. Three
fresh native Metal processes now meet the 33 ms warm first-visible target with
both 512 px and 16 MP tips (p95 26.64–27.04 ms; maxima no higher than 33.99
ms). A one-entry decoded-tip and mip cache removed repeated preset parsing and
mip construction; cold 16 MP first use remains 403–457 ms and is reported
separately. The [latency receipt](evidence/creative-tools/photo-fragments/README.md#native-metal-brush-latency--2026-09-28)
retains the per-process measurements. The 45.18 MP native cancellation run
passes the required 100 cycles and an extended 300-cycle run: all screenshots
restored with unchanged recipe/history, first-visible response was 8.28 ms
p95 / 10.74 ms max, and high-water RSS growth was 33.3 MiB. Final RSS was
1.01 GiB below warm baseline, with no upward trend; separated screenshot
readback was 4.95 ms p95 / 5.36 ms max. The 45 MP cancellation/retained-memory
gate is passed for the tested Mac Metal configuration. B3 still needs
photographic review, restart-and-export verification, and the shared CPU,
tablet, and cross-device gates before qualification. See the
[native cancellation receipt](evidence/creative-tools/photo-fragments/README.md#45-mp-paint-cancellation-stress--2026-09-28).
The release app target now rebuilds successfully with these caches linked.
Fresh native paint/erase/history/export tests and the production MCP composition
contract plus composition workflow tests pass; the broad desktop shell
manual-soak test was skipped by CTest, and the Computer Use bridge was
unavailable, so a visible app walkthrough is still outstanding. Focused logs
are retained in the [Photo Fragments receipt](evidence/creative-tools/photo-fragments/README.md#release-verification--2026-09-28).
Current totals are **9/18** agent runs, **0/9** qualified families,
**0/216** human visual-review cells, and **0/6** finished compositions.


### Smart Select controller follow-up — 2026-09-28

The end-to-end controller regression now passes after fixing two integration
gaps: an 8x8 instance preview had been discarded against a 96x96 decoded image,
and sparse drag validation treated every interpolated background sample as a
required foreground point. Valid preview masks are resized to the commit image,
explicit user prompts are checked separately from interpolation samples, and
meaningful disconnected components are allowed for drags across multiple
subjects while isolated single-pixel prompts remain rejected. `object_pick_policy_test`
and `editor_preview_controller_test` pass; the latter took 137.80 seconds on
this Mac. The controller suite uses the deterministic segmentation provider
for these assertions. SAM2 real-photo accuracy, edge quality, the annotated
corpus gates, and human review remain open. Overall totals are still **9/18**
agent runs, **0/9** qualified families, **0/216** human visual-review cells,
and **0/6** finished compositions.


### Production A5 agent workflow and fresh-text material repair — 2026-09-29

The real-photo production bridge now completes the glass-lettered still-life
brief three times on the licensed, label-bearing Mirto bottle photo using
`qwen3-vl:32b`, the authenticated localhost MCP bridge, and the real
`EditorPreviewController`. Each completed run creates the exact editable
`HELD IN LIGHT` headline, applies typed glass typography to the returned
text-layer ID, keeps the text bounds inside the canvas, and reopens its saved
recipe pixel-identically. The three independent receipts and renders are under
[`A5-attempt-07`](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-29/A5-attempt-07/run-receipt.json),
[`A5-attempt-08`](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-29/A5-attempt-08/run-receipt.json),
and
[`A5-attempt-09`](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-29/A5-attempt-09/run-receipt.json).
The first completed render uses low-strength `frosted_glass`; the contrast-
preserving second and third treatments use `smoked_plastic`, a dark neutral
tint, transmission 0.20, and 34-pixel Georgia. Those renders keep the label
clear and the headline legible beside the bottle. This qualifies agent
execution for A5, not the family’s human taste gate.

That workflow exposed a controller defect when applying version-8 material
typography to newly created point text: the upgrade omitted version-3 effect
defaults and the variable-axis map required by the version-8 style codec. The
controller now supplies those defaults and retains status detail on style
validation or text-fit failure. The focused regression applies material
controls through the production composition command to fresh point text,
checks the version-8 recipe fields, and verifies Undo restores the original
text. The release `photo_typography_workflow_test` passes, as does
`photo_composition_workflow_test` with loopback access. A5 visual review across
the required photos, strengths, and zoom levels remains outstanding.

At this checkpoint, totals were **12/18** agent runs, **0/9** qualified families,
**0/216** human visual-review cells, and **0/6** finished compositions.

### Production A6 contour-text workflow and outline fallback — 2026-09-29

The production composition runner now completes A6 three times with the
licensed monochrome Lyskamm photograph and its matching SAM2 selection. Each
local-model run created the exact editable `NORTH / 04` title, attached the
returned text-layer ID to the selection contour, applied typed typography,
and reopened the saved recipe pixel-identically. The retained receipts,
renders, and recipes are linked for [attempt 03](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-29/A6-attempt-03/run-receipt.json),
[attempt 04](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-29/A6-attempt-04/run-receipt.json),
and [attempt 07](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-29/A6-attempt-07/run-receipt.json).
The [smoothed attempt-07 render](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-29/A6-attempt-07/attempt-final-render.png)
is the current visual reference.
Attempt 03 and 04 exposed readability problems from dense raster-edge samples;
attempt 07 uses the smoothed detached baseline and no longer shows those
glyph collisions. The source is by Giorgio Galeotti under CC BY 4.0;
attribution is included in the pinned corpus record.

This run exposed an asynchronous-outline gap: the first `text_path` request
could arrive before the preview outline worker produced a cache entry. For a
single raster selection, the controller now traces the active selection
directly and applies a bounded three-pixel simplification to the text baseline;
the selection pixels and vector lasso/polygon geometry remain unchanged. A
production-controller preflight confirmed the detached baseline is stored when
the preview outline is empty, and the A6 runner now checks its baseline point
count. A valid single contour does not require a multi-contour `pathContours`
array. Four earlier attempts remain nonqualifying: one hit the async-outline
race, one reached the recipe check before the single-contour assertion was
corrected, and two exposed the mismatch between cached and fallback outlines.

The automated A6 workflow is **3/3**, but it is not a visual acceptance. The
first two retained renders have conspicuous glyph collisions; the third is
cleaner but still needs the plan's human review on multiple photographs,
strengths, and zoom levels. A6 is not a qualified family or finished
composition. That A6 checkpoint recorded **15/18** agent runs, **0/9** qualified
families, **0/216** human visual-review cells, and **0/6** finished compositions.

### A4 production workflow — 2026-10-01

Three counted A4 production-bridge runs now complete the performer-protected
afterglow brief against the licensed Lit concert photograph. Each run uses the
real local model and production MCP bridge to create editable headline text,
typed typography and optical/shutter effects, link the shutter effect to the
performer-protection mask, and add a separate paint layer. Each receipt
verifies exact target IDs, committed paint tiles, visible image change, and a
pixel-identical recipe reopen. See [attempt 13](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-29/A4-attempt-13/run-receipt.json),
[attempt 14](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-29/A4-attempt-14/run-receipt.json),
and [attempt 15](evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-29/A4-attempt-15/run-receipt.json).

Attempt 13 overstates motion and lets the headline collide with the lamps.
Attempts 14 and 15 are cleaner visual references, but still need the plan's
multi-photo, multi-strength, fit/100% human review. Thus A4 is **3/3 workflow
runs**, not a qualified family or finished composition. Current totals are
**18/18** agent runs, **0/9** qualified families, **0/216** human visual-review
cells, **0/18** qualified presets, and **0/6** finished compositions. The pinned
image, mask provenance, and review note are in the [A4 source receipt](evidence/creative-tools/agent-live-workflow/A4-source/README.md).
