# Dimensional and expressive typography

The user's 2026-11-03 priority is richer creative text tools, led by 3D and
perspective. This takes priority over the standalone legacy text-style migration
batch. Existing projects must retain their appearance as a constraint on each
new tool. The first batch now has an implementation in the editor; the acceptance
contract below remains the gate for qualification. Later batches remain planned.

## First batch: editable 3D text and direct perspective

Create dimensional titles in the ordinary Edit workspace. Text, font, character
runs and spacing remain editable. Start with a word, give it depth, rotate it in
space, change its finish and light, and place it convincingly in a photograph.

- Real extrusion with front, back and side surfaces, preserved counters/holes,
  adjustable depth and bevel width/profile. Geometry must respond correctly to
  camera changes; a stack of offset text images does not meet this contract.
- X/Y/Z rotation, a stable pivot, perspective strength/focal length, and a useful
  orthographic option. Neutral settings preserve the original text placement.
- Separate front/side/bevel colors or materials, roughness/metallic controls and
  directional lighting. Material labels must describe the implemented renderer;
  screen-space finishing does not establish scene reflections or refraction.
- Direct canvas rotation/depth controls and a separate four-corner perspective
  mode for placing planar text on signs, walls and packaging. Clearly distinguish
  object rotation, camera projection and planar corner pinning. Reset each mode
  independently and disclose their transform order.
- A compact inspector organized around Shape, View and Surface/light, with useful
  presets such as solid studio lettering, beveled metal and retro deep extrusion.
  Presets remain editable recipes. Existing metal/glass surface looks and the two
  legacy perspective sliders remain readable for older artwork.

Done requires correct holes, overlapping faces, depth occlusion, near-plane and
invalid-camera handling, antialiased transparent edges, explicit resource budgets,
and correct output bounds. Large depth or rotation must not silently crop letters.
Unsupported font/color-glyph combinations need actionable errors or an explicit
supported fallback, preserving the accepted document.

Use production canvas gestures and controller rendering to prove preview-only
drag drafts, one-step commit, Escape/cancel, lock and stale-target rejection,
undo/redo, nested components, masks and crop. Save/reopen and ordinary export must
preserve definitions and agree with preview at declared tolerances. Older recipes
must not change merely by opening them. Native, performance and human visual
qualification stay separate from offscreen/controller tests.

## Following expressive text batches

1. **Warp and envelopes:** arch, wave, flag, bulge and twist, followed by editable
   mesh handles. Deformation stays attached to editable text and composes with its
   depth/projection through a documented order.
2. **Per-letter composition:** adjust individual character position, rotation,
   scale, baseline and depth with stable selection and reset. Preserve ligatures,
   bidirectional shaping and grapheme semantics; do not silently split every
   string into independent letter layers.
3. **Kinetic typography:** animate character/word/line transforms, tracking and
   reveal with editable timing, stagger, easing and timeline controls. Static
   defaults must equal still output; real footage/export qualification is required.
4. **Scene-aware placement:** subject occlusion, editable cast/contact shadows and
   deliberately linked scene cameras/lights. Distinguish geometry and real scene
   relationships from approximate photo-depth or screen-space effects.

Acceptance artwork should include a dimensional product title, type placed on a
receding wall, a warped editorial headline and a short animated title. Review both
at 100% and at final delivery size. Preserve the original text and use shared
instances/local overrides deliberately across photo, graphics and later motion/3D
workflows.

## Current foundations

The current editor already has character formatting, variable-font axes, text on
contours, material looks and a two-slider plane-perspective panel. These are
starting points to extend, not new-feature claims. The full
[professional tools roadmap](photoshop-professional-tools-roadmap.md) and
[capability gates](capability-verification-plan.md) remain authoritative.
An isolated native Debug smoke test was authorized and completed on 2026-11-03.
It covers preset rendering, numeric and canvas rotation, depth dragging,
one-step Undo/Redo, editable source text and Cancel, independent corner pinning,
mask attachment, Library reopen and full-resolution 16-bit PNG export. See the
[native receipt](evidence/dimensional-typography-2026-11-03/native/README.md).

## First-batch implementation boundaries

The text inspector exposes an explicit 3D switch, editable Studio/Metal/Retro
recipes, depth and rounded/chamfer bevels, XYZ rotation, pivot, orthographic or
perspective projection, directional lighting, separate front/side/bevel colors,
roughness and metallic response. Shape starts open; View and Surface disclose the
secondary controls. Rotate, Depth and Corner pin are separate canvas modes, with
an explicit source-text editing action. Corner pins replace the older plane
sliders while active; turning them off restores those saved sliders. The render
order is shaped text -> extrusion -> object rotation/camera -> planar pins ->
ordinary layer transforms, filters, styles and masks.

Geometry is a triangulated glyph outline with depth-tested surfaces, rather than
repeated offset text images. The renderer uses linear floating-point color,
transparent antialiased edges and bounded allocations. Its directional material
response does not model environment reflections, refraction, scene lights,
physical cast shadows or photo geometry. Existing 2D material definitions remain
stored; 3D uses its own surface controls, and disabling 3D restores the 2D look.

Outline-capable fonts, character runs, paragraph/point text, underlines, strokes,
and single-line text baselines are supported. Color glyphs without outlines and
paragraph exclusion are rejected with actionable messages before an edit commits.
Installed font availability and substitutions still require artwork review.
Unsupported camera/horizon crossings and oversized geometry/raster/effect stacks
also leave the accepted state unchanged. No real-time latency claim follows from
correctness checks.

Dimensional envelopes negotiate desktop recipe 21, adjustment patch 5, canonical
provenance v8 with `text-dimension-v1`, and nested component document v3. Absent
new settings retain the older formats. Disabled but configured settings remain
persisted and part of canonical identity. A forward catalog migration accepts the
new capability without rewriting older saved recipes.

The native smoke test supplements the automated checks; native mask painting,
mid-drag cancellation, nested component journeys, real-photo artwork review,
presentation latency and Release qualification remain open. The Export workspace
does not expose the current board-backed canvas; File > Start Export does export
it correctly. New Canvas documents currently reopen under Library > Boards,
which needs clearer naming/routing. These are recorded UX follow-ups, not closed
acceptance gates. Warp/envelopes, per-letter depth/composition, kinetic typography
and scene-aware placement remain the following batches above.

The [scoped Debug receipt](evidence/dimensional-typography-2026-11-03/README.md)
records the implemented first batch, exact checks, rendered examples, source and
binary snapshots, and remaining qualification gates.
