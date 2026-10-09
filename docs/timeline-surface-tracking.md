# 2D surface tracking in the Timeline

Surface Tracking follows one planar patch through a video shot so a title, caption, or image can be attached to that plane. It uses local OpenCV feature tracking; it is separate from the COLMAP Scene Camera solve and does not reconstruct a general 3D scene.

## Workflow

Select an online, untransformed Fit video clip, open **Motion / Track → Track / Cutout**, and use **Surface Tracking**. On the first source frame, draw a seed wholly inside one physical plane, then choose **Track Surface**. If moving objects or another plane overlap the seed, use **Add exclusion** to keep those pixels out of feature extraction, then **Retrack**. Select a title, caption, or image clip, choose the tracked source, and **Attach surface**. Use Surface Placement for width scale, in-plane offsets, and rotation. Detach, placement, and completed analysis are undoable and saved with the timeline.

The tracker extracts features inside the seed, follows them with forward/backward Lucas–Kanade flow, checks a fixed reference with ORB matches, and fits perspective homographies robustly. It rejects low support, invalid geometry, and disagreement between independent estimates rather than inventing samples by interpolation. Feature confidence and fit residual are quality signals; they are not independent measurements of absolute corner accuracy.

### Repair a weak interval

When a non-canceled, non-stale attempt fails, the controller can retain a repair draft containing fully decoded source frames, the original seed, manual anchors, and the failure. It does not accept worker-produced partial motion samples as a valid partial track. Pause on a source frame with visible target corners, choose **Correct corners**, move the four handles to the surface, and apply the correction. The worker treats the corrected quad as a manual anchor, resets its seed mapping there, then measures backward across the preceding interval and forward into the next one. If both directions support a frame, their quads must agree within `max(5 px, 0.8% of the analysis-frame diagonal)`; disagreement fails closed and can be repaired with another manual anchor. The worker may use one-sided support when the other direction has no valid estimate. Cancelled or stale attempts are discarded without committing their output.

The correction is an anchor, not a guarantee that every neighboring frame is recoverable. Each accepted correction requires usable appearance and geometric support around that frame. A successful corrected solve replaces the complete track and removes the draft; a later non-canceled failure can update the saved repair draft with its decoded frames and error metadata. Cancelled or stale attempts are discarded, leaving any prior complete track intact. Existing attachments are tied to a solve identity; after a new solve, reattach a layer if the prior attachment is stale.

## Bounds and limitations

- Analysis stages 2–900 source frames, at sequence cadence capped at 30 fps, with the longest image edge limited to 960 px.
- The worker has a 300-second time limit and a bounded grayscale image cache that retains at most 12 frames, in addition to the current working frames.
- The source must use Fit framing with no clip/group transform, stabilization, rolling-shutter correction, or retiming. Longer shots can be trimmed or split.
- The seed must lie on one physical plane. Moving subjects, mixed depths, blur, repetitive or low-texture surfaces, strong glare, and a plane leaving the frame can cause drift or explicit loss.
- Preview uses the locally retained decoded frames and tracked source-time samples. Export uses the original source video with the saved perspective samples. Missing preview data requires retracking.

This is not a dense depth map, automatic 3D occlusion solve, or relighting system. The separate **Use foreground matte for occlusion** control can composite a generated source foreground mask above an attached layer; the original video source remains complete. Generate that mask from the source with **Generate occlusion mask**; the **SAM2 temporal** option selects the optional temporal model, otherwise supported macOS builds use native per-frame foreground analysis. See [SAM 2 setup and controls](optional-sam2-setup.md) for the temporal workflow.

## Evidence

The current integration evidence and qualification limits are summarized in [surface-tracking evidence](evidence/surface-tracking-next-2026-10-08/README.md). The held-view book-spine result is near its manual-reference uncertainty and is not a moving-camera qualification. The long Galton-board and Heidelberg cases retain their failures and limitations. Historical short-form validation is preserved in [surface tracking validation](timeline-surface-tracking-validation.md).

The optional local OpenCV runtime is described in [surface tracking setup](optional-surface-tracking-setup.md).
