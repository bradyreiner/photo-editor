# RAW detail composition — P8 native review and guarded proposal

**Review date:** 2026-10-30  
**Checkpoint:** P8 saved native snapshot, document hash `16e930e2e2d74566185eb464cf30884fc11547b2a10710df8c8edf9b71dcf860`, file SHA-256 `9cf20c9987dc90e246497005beab04e11c57a69712a896ec7b77a9d528fe9c30`.  
**Review scope:** static native/reference PNG comparison at 44.5667s and approximately 45.0s, plus source-layout inspection. This is not a continuous-playback or audio review.

## Evidence inspected

- P8 native frame 1337 at 44.5667s and native frame 1349 at 44.9667s from `native-raw-pass8-clock-probe/`.
- Reference frame 1337 at 44.5667s and reference frame 1350 at 45.0000s from `raw-pass6-reference-review/`. The second comparison is one frame apart; treat it as a near-match, not a same-time frame comparison.
- [P8/reference comparison sheet](p8-raw-geometry-evidence/p8-vs-reference-1337-1349.png).
- Source composition in [ProofTools.tsx](../../animated-trailer/src/creative-launch/ProofTools.tsx:113): the loupe opens over local seconds 5–6, the panel is 512×605 at `(440,140)`, the grid is 8×8, the selected-cell outline is a separate rectangle, and the connector uses an SVG `strokeDashoffset` tied to the opening progress.
- Native title and vector rendering paths: `ensureTitleFrame` places title shapes at their normalized center in `timeline_document_controller.cpp:49408`; the timeline compositor applies title-frame motion in the `clipVideoFilter` path around `54795–54834`.

## P8 native findings

1. **The detail photo and its stationary outline do not register.** At native frame 1337, the photo plate is approximately `x=648..1608, y=60..1020`; the source’s stationary outline is approximately `x=650..1610, y=82..1042`. The image is about 2px left and 22px high. This mismatch is also apparent in the paired sheet. It is a pose error, not a reason to scale down the demonstrated detail.

2. **The 8×8 grid and selected-cell overlays sit high.** The measured grid top is about `y=345` vs `y=349` in the 44.5667s pair, and about `y=335` vs `y=342` in the near-settled 45s pair. The screen-space discrepancy grows with the panel zoom, pointing to a shared position offset in the grid’s own transformed group. Move all 64 grid cells and 28 lime overlays together by `+7.5px` in their static title center; the existing scale curve then naturally scales that correction during the opening.

3. **The white target outline is displaced from the selected cell.** At the near-settled comparison, the native outline center is about `(1344.1,615)` vs `(1311,587)` in the reference: approximately 33.1px right and 28px down. Correct only the outline’s title position. Do not move the source-pixel marker or cursor to make them agree with the misplaced outline.

4. **The first-panel heading and status arrive late in native.** In the 44.5667s pair, source already shows `SELECTION EDGE / ADD / 1 PX` and the first `SOURCE … SELECTED 25 PX` footer; both are absent in P8 until their current 45s start. In source, these elements live inside the same panel that opens from local second 5. The guarded proposal moves only these two clips’ starts to 44s, copies the opening transform/opacity samples from cyan cell `687c3d34-45c1-4794-800c-6568ec7974e1`, and holds the settled pose. The ADD heading ends at the existing ERASE-heading handoff at 48.5s; the first count ends at the existing next status at 45.9s. Existing copy, styling, and later cards stay unchanged.

5. **The connector is a known feature-parity hold.** Source draws `M(sourceX+8,sourceY+8) L430,160 L460,160` and animates `strokeDashoffset=1-open`. The native vector path is static over the inspected frames; its visible reach differs. I found no native vector-path trim/dash animation action in the authoring surface, so this proposal leaves its geometry alone rather than substituting an unverified transform. Revisit when a path-trim control is available.

6. **Cursor artwork remains a low-confidence visual flag.** In these stills, the native cursor reads as a small white wedge while the reference reads more like a full arrow pointer. This may be an asset/framing difference; two stills are not enough to prescribe a geometry change. Leave it untouched in this pass.

## Exact bounded proposal

The unbound fragment is [raw-detail-geometry-p8-guarded.commands-fragment.json](raw-detail-geometry-p8-guarded.commands-fragment.json). It contains **106 native actions** in 11 bounded groups, with no app calls or project mutation performed here:

- **9 detail photos:** one `setTimelineClipAnimation` per plate, carrying each existing six-key `frameOffsetX` and six-key `frameOffsetY` channel. Only local 2.2s and 10s plateau values change: X `0.35 → 0.3541666667` and Y `0 → 0.0814814815`. This corresponds to +2px/+22px under the native quarter-frame offset convention. Scale, opacity, all other keys, and interpolation remain intact.
- **Separate base curve photo:** deliberately excluded. It is a different wide plate (`a389fd95-11f9-4844-8cf3-b88fa22b6d12`), not one of the nine square detail layers; its opacity is zero through local 2.2–10s. No evidence supports applying the detail-plate transform to it. Its visible entry/exit outside that plateau was not continuously reviewed.
- **Grid registration:** 64 cyan cells plus 28 matching lime overlays, moved together by `+7.5/1080` in normalized title Y. The eight row groups contain 16, 16, 16, 11, 9, 8, 8, and 8 operations, keeping each row’s grid and overlay elements together.
- **Selected-cell outline:** one `setTimelineClipTitle` action moves its center to X `0.70171875`, Y `0.5119444444`. Shape dimensions, stroke, fill, and opacity are preserved.
- **Opening panel:** two paired timing/animation groups. ADD heading: start 44s, duration 4.5s (end 48.5s). First footer: start 44s, duration 1.9s (end 45.9s). Each copies the common parent opening channels from local 5–6s and holds the settled pose. Source sample transforms match the active Landscape override on the parent clip at frames 5.0, 5.1, 5.5667, and 6.0.

The proposal excludes the separate source-pixel marker `a608bc39-132f-4502-a7e4-b703780fb557`, the cursor, and connector path. The batches cap at 16 operations. The two title-copy actions carry 204 keyframe records; all animation steps carry 312 including the 108 photo keyframes. Each batch remains below the 2,048-record limit.

## Required post-apply review

After root binds a new task to a fresh native readback, compare frames 44.0, 44.5667, 44.9667/45.0, 45.9, and 48.5. Check that the photo clears the left/top registration error; all grid and lime cells shift together; the white outline encloses the same reference cell; header/footer scale and fade with the panel; first count and ADD/ERASE cards hand off with no overlap; and the connector/cursor remain explicitly listed as unresolved. No post-edit native render has been performed for this proposal.
