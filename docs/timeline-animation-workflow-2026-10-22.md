# Direct video animation and finishing

## Editing workflow

Select a video or still in the timeline while paused. Program displays its frame:
drag the picture to move it, drag a corner to resize around the opposite corner,
or drag the round handle to rotate. Shift constrains movement to one axis or
rotation to 15-degree steps. Alt bypasses center snapping. Escape cancels a
gesture; one Undo restores the entire move, resize, or rotation.

The contextual Program toolbar contains a parameter picker, a keyframe diamond,
Graph, opacity, and video speed. Add the first keyframe with the diamond, move the
playhead, then edit the property. Existing animated properties add or update a
keyframe at the playhead. Direct transforms preserve authored interpolation and
Bézier tangents and publish the four placement properties together.

Video speed offers 0.25×, 0.5×, 1×, 2×, and 4×, speed ramps, and the existing
source-time curve editor. The button displays a constant rate or “Speed Curve.”
Titles and captions use the same parameter picker and graph for their supported
position, size, opacity, effect, and grading parameters. Their existing direct
text-position controls remain available.

The graph has a larger plotting area, a seconds axis, value fitting, and
double-click insertion at a frame-snapped time and plotted value. Value bounds
remain fixed during a gesture so the graph does not rescale under the pointer.
Precise time/value fields, value/speed modes, easing, weighted linked/broken
Bézier handles, roving keys, and batch operations remain available.

“Fade to Black” and “Fade to White” are available alongside Cross Dissolve in the
transition library. Incoming fades start at the chosen color; outgoing fades
end at it. They change picture color while retaining layer transparency. Program
geometry shaders and ordinary FFmpeg delivery implement the fades. Delivery
evaluates clip-local fade time across bounded render segments and trimmed export
ranges. Cache signatures advance to version 5 so old renders are not reused.

## Boundaries

- Direct placement handles cover ordinary video and still framing. Perspective,
  scene attachment, mask correction, and tracking retain their specialized tools.
  Locked tracks reject edits. Handles are hidden during playback, comparisons,
  fullscreen review, and scope grabs; scope grabs defer during a placement gesture.
- These are placement controls. A true source crop rectangle remains missing.
- The toolbar wraps to the available Program width. It appears only for the
  selected picture clip at the playhead while paused.
- These changes do not close the audio processor, spatial-grade combination,
  cache-resolution, native sync, or sustained 4K performance gates in
  [Video editing readiness](timeline-video-editing-readiness-2026-10-22.md).

## Verification

`timeline_transform_workflow_test` exercises actual QML pointer moves, corner
resize, rotation, Escape, atomic publication, animated transforms, tangent
preservation, locks, non-finite inputs, and save/reopen. It renders real black/
white fades through the ordinary compositor and compares a trimmed export range
against the corresponding full-export frame.

`timeline_animation_ui_workflow_test` loads the actual application QML and
controllers. It clicks the Program keyframe controls, drags the picture, checks
one-step Undo, inserts a graph key by double-clicking, drags a Bézier handle,
checks tangent Undo, clicks 2×/0.5× speed menu items, and opens the shared text
graph. It verifies zero opacity and wrapped controls at 1180×768, waiting for
the Inspector to finish loading. Native runs also sample Program black/white
fade pixels; the software renderer does not qualify shader output.

The full Timeline controller suite passed against its own disposable database.
Eight focused regressions passed: UI consistency, shared curves, keyframe
coverage, effect-keyframe rendering, title-motion keyframes, title-motion
rendering, playback, and frame scheduling. The native Metal scene-projection
fixture passed color-fade and alpha checks as well as its existing occlusion
coverage. The software-QPA shader run correctly skipped that GPU check.

The complete native macOS/Metal UI run passed, including sampled Program black/
white fades and the reopened text graph. Its screenshots were visually reviewed,
including the loaded Inspector at 1180×768. The proof waits for workspace readiness,
menu animations, and the reopened graph's exposure plus a rendered frame before
readback. An earlier premature graph readback crashed inside Qt Metal
`QRhiMetal::beginFrame`; that attempt is retained separately from the passing run.

The [local verification receipt](evidence/timeline-animation-2026-10-22/verification.json)
records matching source/binary hashes, logs, and scope. Captures include
[Program controls](evidence/timeline-animation-2026-10-22/video-transform-controls.png),
[Bézier editing](evidence/timeline-animation-2026-10-22/video-bezier-graph.png),
[text animation](evidence/timeline-animation-2026-10-22/text-keyframe-graph.png),
and [the smaller layout](evidence/timeline-animation-2026-10-22/text-animation-1180.png).
The fixture database was stopped afterward. The fixture-only
`PHOTO_EDITOR_TIMELINE_PROOF_DATABASE_URL` override allows native UI checks to use
a disposable working catalog instead of repeated offline-helper startup attempts.
No product database schema changes are required.

These fixture checks do not establish difficult-footage quality, audible review,
or real-time 4K performance.
