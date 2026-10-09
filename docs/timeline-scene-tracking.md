# Scene camera tracking in the Timeline

Scene Camera reconstructs one shared moving camera and sparse static 3D features from a video shot. A title, caption, or image can be attached to a plane defined by three reconstructed features; its perspective follows the camera and can show depth-dependent motion. The pipeline uses local COLMAP and is separate from 2D Surface Tracking. Sparse points do not provide a dense scene or automatic foreground depth ordering.

## Use

1. Select an eligible online video source in the Timeline. Use Fit framing with clip and group transforms reset; disable stabilization and rolling-shutter correction. Wait for source metadata and thumbnails, then choose **Solve Scene Camera** in the primary **Scene Camera** section of Motion / Track.
2. After the solve, keep the source shot selected and pause the playhead within that shot. In the same Scene Camera section, choose **Select 3 Points**. The point picker displays the cached reconstructed frame at the paused source time (the nearest solved sample at or before it), rather than forcing selection on the first analyzed frame.
3. Choose three widely separated static features on the same physical plane. The first defines the plane center, the second its horizontal direction, and the third its vertical direction; the third point must be away from the line between the first two. Click **Done** after all three are selected. If the paused frame has too few visible reconstructed points, choose another point in the shot.
4. Select a title, caption, or image as the target and choose **Attach Scene**. Adjust width, plane X/Y offsets, depth offset, and rotation in Scene Placement. Scrub the shot to inspect it.

The solve, selected source, point IDs, attachment, and projected samples are saved with the editable timeline. Placement and attachment edits support undo/redo. Scene points can be selected again from the primary section without a separate advanced-3D workflow.

## Foreground occlusion is a separate mask

Three sparse scene points define attachment geometry; they do not identify all foreground objects or their depth. To put a person or object in front of an attached layer, choose **Generate occlusion mask** for the tracked source clip, then enable **Use foreground matte for occlusion** on the scene attachment. The **SAM2 temporal** option generates that mask with the optional video predictor; with it off, supported macOS builds use the native per-frame foreground analyzer. The matte composites the attachment behind the foreground while the source video stays complete and visible. This is separate from enabling a foreground cutout on the video itself. See [SAM 2 setup and controls](optional-sam2-setup.md) for temporal masks.

The mask must match the source identity, trim, and framing. Missing, stale, or incompatible masks disable the occlusion option until regenerated. Inspect mask edges and frame-to-frame stability; this does not provide dense scene depth, automatic occlusion geometry, or qualified fine-hair matting.

## Current bounds and limits

- Scene analysis accepts 8–900 source frames at sequence cadence capped at 30 fps, with a maximum analysis edge of 960 px. Longer shots can be trimmed or split.
- The local COLMAP runtime must be executable on `PATH`, in a standard Homebrew location on macOS, or configured with `PHOTO_EDITOR_COLMAP_BINARY`.
- The solve uses one shared focal length and zero lens distortion. Camera pans without translation do not provide depth through parallax; monocular scale is arbitrary.
- Blur, rolling shutter, heavy lens distortion, scene cuts, repeated patterns, moving subjects, weak baseline, and low static-feature support can cause a solve to fail or drift.
- Supported attachments are flat layers. Export projects the saved per-frame plane samples; a sparse reconstruction does not turn a title into extruded 3D text.

## Evidence

The current feature/evidence matrix and pending checks are in [tracking evidence](evidence/surface-tracking-next-2026-10-08/README.md). Historical scene geometry, controller/export, and native UI records remain in [scene tracking validation](timeline-scene-tracking-validation.md) and its linked evidence. Those historical receipts do not, by themselves, establish qualification for difficult moving-camera footage or foreground occlusion.
