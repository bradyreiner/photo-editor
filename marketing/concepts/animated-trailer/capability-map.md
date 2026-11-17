# Looksy trailer — capability and demonstration map

23 October 2026. Working coverage inventory for the trailer. Source references
below were inspected in this checkout. “Source present” means the command,
controller, UI, or operator exists; publication still needs the depicted real
workflow and rendered output. Older roadmaps must not be treated as current
absence reports or as proof of completion.

This map replaces the practice of summarizing whole workspaces as one headline.
Each major discipline needs a visible creative task, its controls, a revision,
and an output. Technical labels support that story. The inventory is broader
than the eventual on-screen copy; it is not a requirement to flash every command.

## 1. Library and shoot management

**Demonstrate:** mixed-media import → contact sheet → picks/rejects/ratings →
filtered selection → chosen adjustment synchronized across a batch → one outlier
refined individually.

Include batch import, culling, metadata/filter scope, multi-selection, presets/
looks and synchronized edits where the inspected app workflow supports them.
Keep manual library tools visible before the later optional search chapter.

**Value:** handle the real volume of a shoot without losing individual control.

## 2. RAW development and the custom color engine

**Demonstrate:** a real Nepal RAW, recognizable source metadata, white balance,
exposure and highlight/shadow adjustment, an editable curve, color adjustment,
detail, an appropriate scope, and a before/after comparison. Show source detail
at 100%, then inspect the final full-resolution output.

Include the engine as a foundation, not a generic “Color” badge. Distinguish
source/input interpretation, creative adjustments, and chosen output profile.
Use sRGB with the photographic workflow; Rec.709 and the supported Sony log
pairs with the video example. Presets and LUTs should remain adjustable where
the actual tool path supports that behavior.

**Value:** deliberate color and detail from the source through delivery.

**References:** `docs/color-engine-and-app-foundations-completion-strategy.md`,
`docs/timeline-shared-color-engine-integration.md`,
`docs/capability-verification-plan.md`.

## 3. Selection, masks, and local refinement

**Demonstrate:** isolate the subject → inspect the edge → add/subtract with a
brush → adjust feather/coverage → change only the selected region → reopen the
mask and revise it. The original photograph remains recognizable.

Include the distinction between selecting pixels, a layer mask, a local
adjustment, and masking a specific effect. A subject cutout used behind text
is one outcome; a targeted photographic correction is another. Include a
separate small retouch example only after its actual tool/result is qualified;
do not substitute generated replacement content for the stated editing task.

**Value:** refine the part that matters without flattening the whole edit.

**References:** selection/refinement commands in `editor_preview_controller.h`,
`docs/capability-verification-plan.md`.

## 4. Full image composition and graphic design

This is a substantial Photoshop-like authoring chapter, not a text-overlay beat.
Use original product language in the film rather than a competitor comparison.

**Demonstrate:** create a deliberate composition → import a second source image
→ select/copy a source region into an independent layer → refine its mask →
transform and arrange layers → add/edit typography and a shape/fill → paint and
partially erase a mark → alter opacity/blend or a masked effect → undo/revise →
save/reopen the editable result → export the finished graphic.

Coverage to represent across the sequence:

- Multiple source/image layers and independent pixel content.
- Adjustment, paint, fill, text, and shape layers; meaningful layer order.
- Layer masks, selection-constrained work, opacity, blend modes, and clipping
  where the chosen path is qualified.
- Move/scale/rotate and precise placement, not only camera movement around art.
- Point and paragraph text, family/face, size, weight, tracking/leading,
  alignment, and useful text treatment.
- Brush/RGBA paint and pixel-alpha erase as distinct actions from mask painting.
- Editable effect stacks: amount, order, mask, bypass, duplicate, reset.
- Reusable looks and LUTs as creative starting points, not flattened results.
- Visibility/lock/solo, groups, history, and saved editable work; qualify group
  rendering semantics rather than infer them from a group row.

**Finished work:** a Nepal editorial poster plus a web/banner composition
variant. Show actual considered layout, type hierarchy, and multiple image
relationships. The earlier single word over one photo undersells this scope.

**Value:** finish a photograph or build a composed visual asset in Looksy.

**Current source references:** `apps/desktop/src/editor_preview_controller.h`
(selection-to-pixels, point/paragraph text, paint, source/fill/shape/adjustment
layers, layer/effect/blend commands), `photo_paint_session.h`,
`PhotoTypographyPanel.qml`, `PhotoShapePanel.qml`, `PhotoEffectStack.qml`.

## 5. Effects as working tools

**Demonstrate:** at least two distinct effects with meaningful changes to the
picture and an editable stack. Compare bypass/active, change amount, change a
mask or stack order, and retain revisability.

The current photo registry includes optical diffusion, film grain, halation,
anamorphic streaks, lens bokeh, directional/zoom/spin blur, high-pass detail,
gradient map, duotone, halftone, artistic treatments, and displacement. Choose a
small coherent set for the work, rather than rapid previews of the whole list.
Photo operators must not be depicted as timeline operators without verification.

**Value:** create a distinctive photographic or graphic look with controlled,
editable operations.

**Reference:** `engine/include/photo_editor/pipeline/photo_effect_registry.h`.

## 6. Video editing, motion, color, and sound

**Demonstrate:** import distinct moving sources → arrange multiple picture and
audio tracks → trim/split and change timing → add a title/overlay → animate
position/scale or another supported property through its keyframe curve →
apply and adjust a supported video effect → grade a qualified camera-input
example → mix the audio → play and export the finished combination.

Coverage: actual moving footage, source/program context, timeline layering,
transitions, transforms/framing, keyframe timing/easing, effect stacks/masks
where supported, grading/scopes, music/dialogue gain and fades, and output
settings. Include stabilization or advanced audio processing only if it serves
the chosen finished sequence and its combined workflow is qualified.

**Value:** make a full production, with explicit control over picture, motion,
color, and sound. Show 4K output as an inspected delivery specification, not
an implied realtime-performance measurement.

**References:** `TimelineEffectsPanel.qml`, `timeline_document_controller.cpp`,
`docs/timeline-video-editing-readiness-2026-10-22.md`.

## 7. Carousel as a complete design workspace

**Demonstrate:** bring edited imagery into a multi-board project → build a
cover and interior boards → add/reframe photo and video → edit typography and
shapes → use alignment/guides or a repeat layout → link a repeated element
across boards → revise a brand color/type choice and inspect the changes →
reorder or duplicate a board → preview the sequence → choose output scope,
size, profile, and format → export the designed set.

Coverage beyond “unfold three pictures”:

- Multi-board documents, sequence structure, board ordering and variants.
- Freeform media and placed layouts; crop, focal point, source editing round
  trips, and cutout refinement where the selected workflow is qualified.
- Text, shapes, brush marks, layer transforms, opacity and blending.
- Guides, alignment/distribution, repeat grids, reusable layout recipes.
- Editable templates/components; brand kits and text/color tokens.
- Linked repeated elements and cross-board/batch operations.
- Neighbor/continuity preview, preflight, font/media checks and proofing.
- Chosen boards/ranges, image/video output where applicable, dimensions,
  filenames/versioning, and optional proof sheet/manifest.

**Finished work:** a five-board Nepal editorial carousel with a designed cover,
photo-led spreads, a detail board, and a closing board. An intentional graphic
crosses a boundary in the visual sequence; it must be authored using the actual
board/link tools, not imply an unsupported infinite canvas.

**Value:** build and maintain a coordinated social campaign, with reusable
style and control over every board, alongside the photo and film.

**Current source references:** `carousel_document_controller.h`,
`CarouselView.qml`, and the `workspaces/carousel/` dialogs. The 100-item carousel
checklist is a historical implementation/evidence guide, not a fresh assertion
that every listed combination is production-qualified.

## 8. Delivery and retained editability

**Demonstrate:** inspect the full-resolution photo, designed graphic, encoded
film, and exported carousel panels; reopen the working project and change an
edit. Match dimensions, profiles, and formats to those actual outputs.

Include practical batch/selected export, transparent graphic output where
supported, version/naming control, save/reopen, and history/revisions. RAW
editing and source-resolution export must not be called camera-RAW export.

**Value:** deliver client work and retain the ability to revise it afterward.

## 9. AI search — its own user-controlled feature

**Demonstrate:** after the complete manual workflow, reopen the library and
search **“a person in a yellow jacket with mountains behind them.”** Start from
a visible selected collection/filter scope. Show progress and real matches,
inspect a result's visible-content explanation, select the intended photographs,
then explicitly choose **Edit selected**. Search alone changes no image.

A second query can demonstrate content rather than filenames, such as
**“orange flowers”**, if the chosen shoot and selected local model return the
expected real matches. Do not invent relevance or use an instant all-library
search animation that implies unmeasured throughput.

Explain visually, with short readable cues:

- **Describe what you remember:** natural-language visible-content search.
- **See why it matched:** result evidence and human selection.
- **Search locally:** a verified local vision-model configuration.
- **Keep useful analysis:** saved visual descriptions can be reused on later
  searches; detail checks can inspect a photo when the description is insufficient.
- **Choose what happens next:** selected matches can be handed to an edit;
  retrieving a result is not permission to mutate the entire library.

Production details belong outside the film: current controller work is bounded
in explicit 32-photo batches, supports continuation/cancel, intersects metadata
filters, and offers saved-description reuse or direct inspection. The stored
index is a description cache, not an embedding/instant-nearest-neighbor engine.
Do not claim face identity, video search, exhaustive object detection, perfect
negation, automatic background indexing, or autonomous library-wide culling
from this implementation. Validate the exact searches on the actual Nepal set.

**Value:** find photographs by what is in them, then keep control of selection
and editing. This deserves a distinct beat from optional editing assistance.

**Current references:** `visual_search_controller.h/.cpp`,
`docs/visual-search-persistent-index-2026-09-06.md`,
`docs/visual-search-detail-verification-2026-09-08.md`.

## 10. Optional local agent — repeat work with review and revision

**Demonstrate:** connect an installed local model → ask for a bounded task on
explicit selected assets → review the proposed direction/sample → change that
direction if needed → approve the batch → open a real result's native controls,
layers/masks and history → revise it manually → undo/save/reopen.

Illustrative prompt to qualify in the real app: **“Apply the approved color
adjustments to these selected portraits. Keep the originals and flag images
that need individual review.”** Show actual supported tool operations and real
outputs; do not assert automatic outlier recognition until the result is proved.
A separate design-assistance example may demonstrate native text/layout edits
if the selected local model can complete and preserve the composition.

Keep search, proposed edits, execution permission, sample review, and the final
editable state distinct. Do not imply every tool operation creates a new layer
or that every model configuration has the same ability. In the pictured local
mode, inference stays on the user's machine. Existing media is edited; no
replacement photographs/video are generated in the showcased workflow.

**Value:** optional help with repeat work while creative decisions and the
working edit remain with the professional.

## Coverage gate before another polished render

- Use the [actual app-element kit](app-elements.md): sliders, value/reset
  anatomy, app icons, native optical loading/progress/completion and original
  brand assets. Derive remaining UI from the actual workspace components.

- Photo/RAW, image composition, video, and carousel each have a finished output
  and a meaningful revision, not a headline alone.
- Custom engine, source detail, pixel tools, masks, effects, color workflows,
  and delivery settings appear through actual pictured decisions/results.
- Search has a query, scope, progress, match evidence, selection, and explicit
  handoff. Agent assistance has a separate request/review/editability sequence.
- Audit each storyboard against this map before shortening it. If a major
  discipline disappears, revise the story/duration rather than hiding it in a
  list or squeezing it into a three-second montage.
- Keep the dark optical identity, additive RGB, abstract 2D camera continuity,
  no voiceover, no character, and editor-first / optional-AI-later hierarchy.
