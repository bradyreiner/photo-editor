# CTA shape geometry audit — P4 readback

## Result

The closing lime button is authored twice at the same normalized size and position. Its inner shape is `.19921875 × .0831018519` at `titlePositionY=.6608401037`; the outer frame repeats that size and its baseline Y offset is `4 × (.6608401037 − .5) = .6433604147`. The generated title first draws the shape in a sequence-sized frame, then the outer frame scales that full-canvas title again. The resulting button is about **76 × 7 px** instead of the title-shape dimensions of **382.5 × 89.75 px**.

The native closing frame at `290.9667s` visually confirms the tiny underline. The source recipe confirms this came from the authoring commands: `createTimelineGraphic` op007 writes the inner shape dimensions and position, and `setTimelineClipFrame` op008 repeats the dimensions and maps the outer frame offset to the same position.

## Recommended CTA-only correction

The unbound command fragment [closing-cta-frame-geometry-v1.commands-fragment.json](closing-cta-frame-geometry-v1.commands-fragment.json) contains two guarded native actions:

1. Set only this CTA's outer frame to full canvas (`width=height=1`, offsets and outer corner radius `0`). Keep the title shape size, corner radius, color, and title position.
2. Rebase only the `frameOffsetY` keyframes by subtracting the settled baseline `.643360415`. The four values become `.111111111, .111111111, 0, 0`, preserving the existing 30 px downward entrance and the 8.5–9.0s ease-in-out settle. Leave opacity keys untouched.

At 1080 px sequence height, the retained shape position yields a center near `y=713.7 px` when settled and ~30 px lower at the entrance. This matches the existing title position and retains the current movement.

## Other shape clips

P4 has 320 title-shape clips. 44 have identical inner and outer dimensions; 43 also satisfy the mapped-position relation above. Five are neutral full-canvas background shapes. The remaining **38 candidates** include the CTA and 37 other small rectangles, ellipses, UI marks, panel edges, and possible masks. One more clip has matching dimensions but does not satisfy the mapped-position relation; it is recorded separately and left out of the strong-pattern candidate set. They use a repeated authoring pattern, but their intended visual roles differ. Do not batch-correct them. Review representative native frame groups at their scene times and classify each before proposing an edit.

The other 37 candidate clips cluster at 196–252s: 9 begin at 196s, 1 at 199.5s, 7 at 208s, 9 at 218s, 3 at 226s, 4 at 232s and 4 at 244s. Full candidate IDs, dimensions, offsets, track groups and frame-key counts are in the JSON audit. No project, controller, UI, or running native document was modified by this audit.

## A/B render review

Sample native output at 288.5, 288.75, 289, 290 and 297 seconds. Inspect the ROI `(730,625)–(1190,800)` for a full-size button, a legible centered label, the 30 px settle, and a stable hold. Compare both the native preview and export: the QML Loader also sizes from the outer frame and the title shape scales within it, so the project correction should address both paths. If preview and export disagree, stop and investigate parity rather than making a second geometry guess.

The authored width predicts 382.5 px; the reference screenshot's rough 410 px estimate should be checked in the root's render review before changing the accepted inner shape width.

## Guard and scope

Bind the exact live document hash/resource/task from fresh native inspect at admission, then require the exact CTA P4 fields and landscape override snapshot in the fragment before executing. Any mismatch aborts. The P4 saved snapshot is a planning baseline, not an execution hash. The fragment targets one stable clip ID and is reversible through the exact task baseline.
