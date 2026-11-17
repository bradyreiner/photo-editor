# Painted realism and brush motion — v2

This revision uses two built-in imagegen paintings as visual keypose references.
They are saved locally with their exact prompts under
`marketing/outputs/after-the-rain-v2/`. The reference images are AI-generated;
this revision is not an exclusively Looksy-authored original drawing.

Exact built-in imagegen prompts:

- [Opening painted keypose](../../outputs/after-the-rain-v2/reference-prompt.txt)
- [Reaction keypose edit](../../outputs/after-the-rain-v2/reaction-prompt.txt)

`plan_reference_brushes.py` analyzes reference colors, gradients and contours.
It writes a brush score and does not render an intermediate painting. The native
`native_brush_score_driver` uses Looksy's production `PhotoPixelStroke` to draw
each ordinary colored stroke. No reference bitmap or photo-fragment brush is
inserted into the paint layers. The first frame contains 382,511 native marks,
retained in six native paint layers with a separate replayable stroke score.

The static project uses Looksy's existing direct rendering model for
display-referred painted artwork. It saves a transparent blank source and a
native document, then exports with the production flattened renderer. Re-reading
that saved document with `native_canvas_export_driver` produced pixel-identical
output. This does not establish foreground app reopen or interactive export UI.

For motion, `track_painted_poses.mm` analyzes the references using macOS Vision
optical flow and facial landmarks. The Objective-C utility is the active tool;
the earlier Swift source is retained but does not build with the installed
Swift/SDK combination. `plan_brush_motion.py` combines local motion fields with
facial controls, and writes trajectories and colors for the original marks.
Native motion frames redraw the strokes with those trajectories and an overall
camera push. This is keypose interpolation, not manually authored drawings for
every exposure, generative video, or a complete 3D camera reconstruction.

Visual review must reject folded contours, facial distortion, a sliding hand,
ghosted edges and unstable paint before a test is treated as a usable shot.
The current study is an animation-method test, not the finished 36-second film.

The delivered test is `after-the-rain-motion-v2.mp4`: 1600 × 900, four seconds,
24 fps, 96 distinct native brush-rendered frames, with an original synthesized
sound cue. The final score is `motion-brush-score-v3.json`. Semantic stroke
ownership prevents an endpoint crossing from stretching a brush across two
objects; local background marks fill the region revealed by the moving head.
The hand and flower remain fixed in the shared scene. Native in-between review
removed large boundary streaks and hand/flower distortion. Head/neck and edge
cleanup remain; full-speed human playback is not claimed as qualified.

The six-layer static native document is registered in the isolated Looksy Painted
Review catalog and read back exactly. Its independent saved-document export is
pixel-identical to the initial export. `motion-receipt.json` records frame hashes,
the fixed renderer hash, actual rendered mark counts, media inspection and the
limits of the current review.
