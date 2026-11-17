# P10 duplicate-shape groups: guarded proposal audit

## Result

The remaining definite groups from the P6 proposal are suitable for **small P10-bound proposals**, but none is ready to apply without an actual native-frame baseline. I compared all 16 target clips in groups 196, 226, 232, and 244 against the saved P10 checkpoint. Every listed precondition field matches: stable clip ID, parent track ID, generated asset ID, placement/trims, title shape and text styling, outer frame, opacity, normal animation channels, and landscape frame/framing-keyframe overrides (numeric tolerance `1e-9`). The long animated X channels for the two playheads also match their saved values. P9’s 218s group is excluded.

The four per-group JSON fragments preserve the original P6 action values and curves, but replace the planning guard with P10’s saved checkpoint. They remain unexecuted proposals with fresh-task placeholders. The native-frame capture times are embedded in each fragment. The 12 previously held candidates remain out of scope.

- [196s editor UI, 9 clips / 11 actions](chapter-196-editor-ui.p10.commands-fragment.json)
- [226s audio lanes, 3 clips / 5 actions](chapter-226-audio-lanes.p10.commands-fragment.json)
- [232s Rec.709 tag, 1 clip / 1 action](chapter-232-source-label.p10.commands-fragment.json)
- [244s delivery pills, 3 clips / 3 actions](chapter-244-delivery-pills.p10.commands-fragment.json)

## What the renderer math supports

This is a duplicate outer/inner geometry issue, not a request to reauthor the visual elements. The render path calls `ensureTitleFrame()` with the output target size ([call site](../../../../apps/desktop/src/timeline_document_controller.cpp:51612)); it computes inner shape bounds from `titleShapeWidth/Height` and the center from `titlePositionX/Y` ([shape geometry](../../../../apps/desktop/src/timeline_document_controller.cpp:49643)). The clip filter then independently derives `frame_width/frame_height` from `frameWidth/Height` ([frame dimensions](../../../../apps/desktop/src/timeline_document_controller.cpp:54890)) and scales the source image to those dimensions for `stretch` clips ([frame-mode filter](../../../../apps/desktop/src/timeline_document_controller.cpp:55503)). Frame scale and offset/crop are additional transforms ([title-position adjustment](../../../../apps/desktop/src/timeline_document_controller.cpp:55108), [frame scale/crop filter](../../../../apps/desktop/src/timeline_document_controller.cpp:55777). For example, the 196s shell repeats a width of about `0.573` in both inner and outer geometry: the second scale predicts roughly `0.573² = 0.328` of canvas width (about 630 px at 1920px), while the reference shell spans about 1100 px. That prediction comes from the stored state and renderer math, not a captured native output; the native baseline remains required.

For the selected duplicated shapes, `frameWidth/Height` repeat the inner `titleShapeWidth/Height`. That makes the already-canvas-sized shape image pass through another scale by the same normalized dimensions. Neutralizing only the outer frame to full-canvas dimensions removes that second size reduction while retaining the inner authored shape size, center, fill, stroke, text, and other style. Animated frame-offset channels must still use the proposal’s explicit clear/set/restore sequence so `setTimelineClipFrame` does not replace authored motion at the current playhead. These are source-derived expectations; actual native screenshots must confirm the correction and animation before acceptance.

## Priority for the next native baseline

1. **196–208s editor UI, capture 199.5s first.** This is the strongest next group after the already-corrected 218s effects scene. The source reference shows the complete editor mockup with shell, preview, inspector, title lane, four video labels, and moving playhead in one frame. A baseline at 199.5s tests all nine proposed elements together; 196s, 207.9667s, and 208s bound the entrance/hold/exit. If the native output confirms the repeated scaling, the 11-action group is a contained correction with a useful before/after frame.
2. **226–232s audio lanes.** This is the next most valuable moving proof: two lane backplates and a playhead traveling over them. Capture 226s, 229s, 231.9667s, and 232s before changing it. Keep the full authored playhead curve.
3. **232–240s Rec.709 source tag** and **244–252s delivery pills** are visually straightforward static groups, but their feature proof is narrower. Baseline them after the two broader editor/audio scenes.

## Evidence boundary and holds

The available images at these beats are the source-reference frames in `shape-geometry-review-p6/reference-frames/`. The P10 file is a saved native project snapshot, not a rendered-frame proof. I found no native rendered frames for 196, 226, 232, or 244 in the current review artifact set. Therefore this review does not claim that the live rendered sizes, motion, or before/after pixel match have been observed.

Keep the twelve held candidates untouched: G42 at 199.5s; all seven 208s motion candidates; the three fine rules at 232s; and G9 at 244s. Their roles or intended endpoints remain uncertain from the available reference stills. Do not add them just because the proposed static groups are approved by geometry inspection.

## Reproducible provenance

- Planning snapshot: `native-projects/looksy-polish-pass10-new-gods.json`
- Project file SHA-256: `baccbd99bfdd7878b6de217a7e65727f194935f89cf4fcc77aa7801ea6a3833f`
- Native document hash recorded for P10: `a233a960dc625960461cdbb4d5b0343ed47c10c6e81e0c552d8c348a0612d6b6`
- Sequence: 1920×1080, 30 fps, 298 seconds
- Groups validated: 196 (9 clips / 11 actions), 226 (3 / 5), 232 (1 / 1), 244 (3 / 3). Each group is below the 16-action batch limit.
- Scope: read-only snapshot/source/reference audit plus new proposal artifacts. No native project, live worker, app source, or original asset was changed.
