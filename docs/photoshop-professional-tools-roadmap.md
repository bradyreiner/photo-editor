# Professional content tools: Photoshop foundations and cross-media roadmap

Approved: 2026-10-29. Scope: professional photo compositing and graphic design first, with reusable foundations for animation, video and 3D. The user authorized implementation with **Luna agents at xhigh**. Existing editing, selection/mask, typography, effects, timeline and scene work must be preserved.

This is the implementation specification. Authoritative item status lives in the [capability acceptance owner](capability-verification-plan.md#professional-content-tools-2026-10-29). Existing masking, performance and timeline owners retain their scopes. Prior Photoshop plans are historical baselines; their old missing-feature lists do not describe the current tree.

## What counts as done

A tool is **Complete** only when its entire item contract below and these shared gates pass. An engine primitive, visible button, successful build, or isolated test is a narrower milestone.

1. **Usable workflow:** reachable from the actual workspace, direct canvas/inspector interactions, discoverable target feedback, working menus and keyboard commands, explicit Apply/Cancel where appropriate. No enabled placeholders.
2. **Editable data:** preserve original media, paths, text, masks, transforms and parameters. Duplicated/shared/independent content has explicit identity. Layer, mask and working selection remain separate targets.
3. **History and safety:** one undo per logical operation or gesture, complete redo, cancellation restores the prior state, locks are respected, async results validate document/target/revision, failures preserve accepted edits.
4. **Durable documents:** recipe migration, asset persistence, portable dependency handling, save/reopen, and recovery preserve exact definitions and visible output. Unsupported versions fail clearly or retain opaque data without silently rendering it incorrectly.
5. **Rendering:** preview and ordinary export use the same semantics, including masks, soft alpha, working color space, nested groups, transforms, crop and output resolution. Backend fallback is explicit; unsupported accelerated paths must not skip edits.
6. **Interaction quality:** verify mouse/keyboard input through production QML at standard and compact window sizes and Retina scaling. No overlapping controls, layout shifts during gestures, stale hover/menu state, or missing final stroke. Native evidence is reported separately from offscreen Qt simulation.
7. **Quality and performance:** use representative photographs and design files; test small/large documents and mixed stacks. Measure response and memory on a named machine/backend. Targets: 60 Hz feedback, pointer handler p95 <=4 ms, brush feedback p95 <=33 ms, parameter preview p95 <=100 ms on a 2560-edge preview, cancellation acknowledgment <=100 ms. A missed/unmeasured target remains open rather than being called complete.
8. **Proof:** focused tests assert behavior and rendered results; an actual app workflow covers create/edit/undo/save/reopen/export. Record source/binary hashes and visual captures. Real-media/human quality review, native interaction, packaged builds and realtime performance have distinct statuses.

Implementation receipts: [Batch 1 foundations](evidence/photoshop-professional-tools-batch1-2026-10-29/README.md), [Batch 2 linked sources, vector masks and Pen authoring](evidence/photoshop-professional-tools-batch2-2026-10-29/README.md), [Batch 3 editable components, mask coordinates and SVG](evidence/photoshop-professional-tools-batch3-2026-10-29/README.md), [Batch 4 component canvas editing](evidence/photoshop-professional-tools-batch4-2026-10-30/README.md), [shared storage, authored RAW/HDR and Layer Styles](evidence/component-storage-layer-styles-2026-11-02/README.md).

## Delivery order and dependencies

Batch 1 starts three bounded foundations in parallel: reusable Smart Object content, editable vector paths, and Blend If. The integration owner handles shared controllers, recipe/render wiring, workspace menus, CMake registration and final builds. Agents own separate files and must not run competing builds in the same directory.

Follow with shared layer styles and richer layout/typography; then warp, advanced mask finishing, channels and automation. Extend proven photo tools into timeline/scene workflows after their static behavior is correct. Each slice must be useful and reversible, and remaining item gates stay explicit.

## Photoshop-style tools

### PS-PRO-001 — Smart Objects and reusable components

**Purpose:** edit source content once while preserving every placement's transforms, masks and effects.

**Complete when:** embedded and linked content policies are explicit; selected content/groups can become an editable nested document; shared instances update together while Make Independent breaks the link; Replace Contents preserves placement identity and outer masks/styles/transforms; nested text/shapes remain editable; missing links have relink/recovery controls; cycles and excessive nesting fail safely; preview/export/reopen agree. Opening/editing/closing nested content has a clear return path and atomic history.

**Acceptance scene:** place a product component three times, style/transform each differently, edit its internal text, replace the product image, make one instance independent, undo/redo, save/reopen and export. Two shared instances update and the independent one does not.

**First slice:** versioned reusable source-content model, independent/shared instances and Replace Contents with preserved outer layer state. Batch 3 adds bounded editable nested documents, child editing/replacement, atomic Apply/Cancel and ordinary export/reopen coverage; Batch 4 adds isolated nested editing in the ordinary canvas, per-frame selection/history, layer tools and atomic return. Component source-based selection and retouch now have [scoped workflow evidence](evidence/color-range-component-retouch-2026-11-01/README.md). Object-table storage, authored RAW/HDR and the first shared Layer Styles slice now have [scoped Debug evidence](evidence/component-storage-layer-styles-2026-11-02/README.md); the [checklist](component-storage-and-layer-styles-qualification.md) keeps remaining item gates explicit. Native/performance and full item gates remain open.

### PS-PRO-002 — Pen tool, paths and vector masks

**Purpose:** precise cutouts and scalable graphic authoring.

**Complete when:** create/edit open and closed cubic Bezier paths; add/remove anchors; linked/broken handles and corner/smooth conversion; direct selection, snapping and numeric positions; path union/subtract/intersect/exclude; fill/stroke controls; vector masks and path/selection conversion; scalable preview/export, SVG interchange with a declared supported subset, and exact undo/reopen. Path editing does not silently rasterize or move image pixels.

**Acceptance scene:** trace a curved product, revise handles, subtract a hole, attach its vector mask, create a stroked graphic from the same path, resize and export at multiple resolutions with smooth edges.

**First slice:** durable cubic path model and rasterization/validation, followed by a real authoring overlay and layer/mask integration.

### PS-PRO-003 — Blend If and advanced blending

**Purpose:** blend graphics, textures and lighting by the source/underlying tonal ranges.

**Complete when:** source and underlying black/white split sliders; smooth feathered transitions; gray and individual RGB channels; reset/bypass; explicit HDR/color evaluation semantics; independent Fill/Opacity and documented channel/knockout behavior; correct masks, alpha and group combinations; live inspector feedback; history, persistence and export parity.

**Acceptance scene:** blend paper texture into highlights while protecting shadows, split the transition to remove hard edges, adjust an underlying layer and observe the blend update, undo/reopen/export without baking.

**First slice:** source/underlying tonal gates and RGB/gray choice through real layer rendering and UI. Fill/Opacity separation and knockout remain separate open subgates.

### PS-PRO-004 — Shared Layer Styles

**Complete when:** nondestructive ordered strokes, inner/drop shadows, inner/outer glow, color/gradient overlays, bevel/emboss on images, text, shapes and groups; reusable/copyable styles; canvas handles where useful; scale behavior, soft-alpha boundaries and global-light policy are explicit; styles remain editable after resize, reopen and export. Existing text stroke/shadow controls migrate without changing old artwork.

**Acceptance scene:** create a branded headline and product badge, share their style, revise lighting and size, and compare reopened full-resolution output.

**Authoring slice:** [gradient and style-mask authoring](hdr-retouch-style-authoring-qualification.md) adds stop editing, gradient strokes and independent style-mask create/edit/replace/clear through the actual inspector and shared mask paint target. The [canvas and lighting slice](canvas-style-and-compound-mask-qualification.md) adds shadow/gradient handles, preview-only drafts, opt-in document lighting and compound mask/layer qualification. The [interaction cleanup](mask-layer-ui-qualification.md) adds explicit mask/artwork target switching, menu guards and shared control/numeric polish. Native authoring, legacy text-style equivalence and performance remain open.

### PS-PRO-005 — Mesh Warp and Puppet Warp

**Complete when:** editable mesh/pins, anchored regions, pin rotation/depth, mesh density controls, predictable transform order, linked/unlinked mask policy and bounded deformation; Apply/Cancel and undo; preserve source resolution and definitions; preview/export agree. Existing perspective/Liquify behavior is preserved.

**Acceptance scene:** wrap art around a package and pose a cutout with anchored feet, revise pins after reopening, then export without destructive resampling of the source.

### PS-PRO-006 — Professional typography

**Current priority (2026-11-03):** [dimensional and expressive typography](dimensional-typography-plan.md), led by editable 3D extrusion/bevel and direct perspective placement, followed by warp/envelopes, per-letter composition and kinetic type. The user put these ahead of a standalone compatibility batch; old-artwork equivalence remains a constraint on every new tool.

The first dimensional batch adds editable mesh extrusion/bevel, object/camera
controls and separate corner pins in Edit, with live canvas drafts, source-text
editing and component persistence. The [implementation boundaries](dimensional-typography-plan.md#first-batch-implementation-boundaries)
record supported fonts/layout, resource limits and the following warp/motion work.
This does not close the broader PS-PRO-006 acceptance checklist.

**Complete when:** point/paragraph text, mixed style runs and existing fonts remain intact; text on editable paths; reusable paragraph/character styles with local overrides; kerning, glyph access and supported variable-font/OpenType controls; clear overflow and missing-font handling; consistent caret, IME, draft, preview and export layout. Text remains text through save/reopen and component reuse.

**Acceptance scene:** a poster with a curved headline, multi-column-style text boxes and shared styles; revise copy and fonts, fit text deliberately, undo and export with matching line breaks.

### PS-PRO-007 — Coordinated artboards and design variants

**Complete when:** extend existing Boards/snapshots to independent output bounds, guides/safe areas/bleed, shared components and explicit per-layout overrides; compare Layer Comp-style alternatives without overwriting artwork; align/distribute/snapping across relevant selections; batch export each board/variant with deterministic names, dimensions and color profiles; reopened layouts are unchanged.

**Acceptance scene:** one campaign in square, portrait, landscape and video-title formats; revise one shared product/headline and preserve intentional local positioning and overrides.

### PS-PRO-008 — Channels and calculations

**Complete when:** inspect RGB and named alpha channels; duplicate/edit/import/export alpha; nondestructive channel arithmetic with explicit working-space/precision rules; load/combine channels as selections or masks; offer an editable texture/tone separation workflow with independently controlled reconstruction; zero unintended change outside accepted coverage.

**Acceptance scene:** derive a difficult cutout from channels and retouch tone independently of texture, preserving fine details through reopen/export.

### PS-PRO-009 — Select and Mask finishing

**Complete when:** source-resolution edge refinement on hair/fur, holes, translucent material and glass; color-spill cleanup; foreground/background comparison backgrounds; clear radius/feather/shift/density controls; original/processed comparison; explicit output to selection or layer mask; safe async cancellation/retry and model availability feedback. Qualify actual photographic results and preserve deliberately soft alpha. Existing independent-selection behavior is mandatory.

**Acceptance corpus:** hair against mixed backgrounds, fine fur, bicycle spokes/holes, translucent fabric and glass. Retain originals and have a person review edges at 100% and at final output scale; synthetic tests alone cannot complete this item.

### PS-PRO-010 — Actions, batch production and delivery preflight

**Complete when:** record supported editor commands with editable parameters and explicit targets; replay deterministically across a selected batch; pause/skip/stop and per-item failures preserve documents; report unsupported operations honestly; export variants and check dimensions, fonts, linked assets, transparency and profiles. Provide a documented layered interchange subset and explicit unsupported-feature reporting rather than promising universal PSD/PSB fidelity.

**Acceptance scene:** record a product treatment, run it on a mixed-size batch, replace copy/image variables, export several formats, recover a missing asset and retry only failed items.

## Cross-media extensions

### PS-PRO-011 — Persistent subjects across media

**Complete when:** carry a named subject/selection into tracked video masks and scene placement with confidence and occlusion handling; allow frame corrections and propagation limits; retain keyframes and edits through undo/reopen/export. Qualify difficult motion and occlusion on real footage, with no identity jumps hidden by interpolation.

### PS-PRO-012 — One design, multiple deliverables

**Complete when:** text/color/component data can be shared deliberately across a still, carousel, animated title and product scene; local overrides survive global changes; animation timing is not flattened; batch output resolves the same asset versions and reports missing dependencies.

### PS-PRO-013 — Depth-aware compositing

**Complete when:** place photos/text/graphics relative to real scene depth, with editable occlusion, shadow receiving, camera placement and focus; share preview/export geometry and color; expose approximate depth and uncertain edges honestly. Qualify camera motion and transparent edges on real scenes.

### PS-PRO-014 — Animate existing artwork

**Complete when:** layer, path, mask, style and warp-pin parameters can be animated from their existing controls and shared timeline; interpolation/easing, graph editing, parenting and timing are explicit; static defaults match still rendering; video/image-sequence delivery and reopened projects retain the same motion.

## Parallel work rules

- Use `gpt-6-luna`, reasoning `xhigh`, for the requested implementation agents.
- Three agent lanes initially: Smart Object model; vector path model; Blend If compositor. Agents must inspect existing code and deliver functional code plus behavior tests, integration contracts and honest remaining gates.
- Shared tree is heavily dirty. Never reset, stash, clean, overwrite others' work, restart user editors, or commit without instruction. File ownership is assigned per batch; request integration through the owner rather than editing another lane's files.
- Integration owner alone changes shared controller/workspace files and CMake lists, runs serialized builds, reviews code, tests production routes and updates canonical status.
- Complete means all applicable gates above passed. Partial foundations are recorded as **In progress**, with exact evidence and remaining work. No UI/engine/source-only milestone is promoted to full native or professional qualification.
