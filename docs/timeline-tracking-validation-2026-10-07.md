# Timeline tracking validation

This implementation adds local object-box motion tracking, independent foreground
cutouts, and optional seeded SAM2 temporal segmentation. See
[research and architecture](timeline-tracking-research-2026-10-07.md) and
[usage, runtime setup, and limits](optional-sam2-setup.md).

## Focused checks

- `timeline_motion_runtime_test`: portable bounds, normalized region validation,
  and unsupported-platform behavior passed.
- `timeline_motion_runtime_macos_test`: real Apple Vision followed a moving
  synthetic target over ten frames; normalized x changed from 0.290625 to
  0.414415. The photographic foreground fixture produced nonempty native masks.
  This ran outside the filesystem sandbox because sandboxed native image-buffer
  allocation did not provide a usable Vision execution path.
- `timeline_foreground_matte_test`: source identity/range validation, frame lookup,
  original-alpha multiplication, resized masks, and missing-matte handling passed.
- `timeline_sam2_runtime_test`: subprocess protocol, progress, cancellation,
  timeout, prerequisites, and output validation passed using fake workers. These
  checks do not establish model quality.
- `timeline_tracking_panel_ui_test`: actual Qt control clicks and rendered panel
  passed, including seed payloads, attachment, cancellation, and cutout readiness.
  [Panel capture](evidence/timeline-tracking-2026-10-07/tracking-panel.png).
- `timeline_tracking_workflow_test`: attachment creates editable position keys;
  existing position animation is protected; undo/redo, document reopen, and
  cutout toggle history passed. An actual FFmpeg render was decoded and checked:
  the red source remains in the visible half and disappears in the masked half.
  Missing sidecars block export.
- The optional real-model controller run also passed native foreground analysis,
  immediate cancellation without replacing the previous matte, SAM2 propagation,
  document reopen, and actual FFmpeg export. The one-second four-frame portrait
  used SAM2.1 Tiny on CPU at 410 by 512 pixels; the SAM2 analysis plus subsequent
  reopen/export assertions completed in approximately 3.31 seconds.
- A second real-model run used a four-frame horizontal pan of the portrait and
  passed the same controller/reopen/export path. Inspect the
  [source video](evidence/timeline-tracking-2026-10-07/portrait-pan-source.mp4),
  [cutout render](evidence/timeline-tracking-2026-10-07/portrait-pan-cutout.mp4),
  [first rendered frame](evidence/timeline-tracking-2026-10-07/portrait-pan-cutout-first.png),
  and [last rendered frame](evidence/timeline-tracking-2026-10-07/portrait-pan-cutout-last.png).
  The first mask has visible holes in dark clothing; the last mask is more solid.
  This is useful first-pass segmentation and still needs inspection/refinement.
- The isolated Cocoa `desktop_shell_timeline_workflow_test` passed the full
  workflow, including tracking-panel pointer clicks, decoded first-source-frame
  seed dragging, and return to the panel. Offscreen AVFoundation did not supply
  a usable initial decoded frame; native Cocoa was used for interaction proof.
  [Native panel](evidence/timeline-tracking-2026-10-07/native-tracking-panel.png)
  and [first-frame region HUD](evidence/timeline-tracking-2026-10-07/native-seed-selection.png).
- The desktop application build, headless CMake configuration, launch-script
  syntax, and worker Python syntax passed.
- The final packaged debug app launched with isolated preferences/data on Cocoa,
  reached the production Timeline route, and exited through the existing route
  probe without QML load errors. This startup check is not a playback performance
  qualification.

## Evidence boundaries

The portrait fixtures are a static photo and a simulated horizontal pan encoded
as short videos. They prove the real model/controller/export connection and
propagation across changing frames, not accuracy under occlusion, perspective
camera motion, fine hair, transparency, or long shots. SAM2 emits thresholded segmentation
masks. Native foreground masks are independent per frame and can flicker.

Placement currently follows translation through ordinary timeline position keys.
It does not solve a 3D camera or planar corner pin, nor follow scale/rotation.
Analysis is capped at 640 pixels, 900 native frames, and 180 SAM2 frames. MPS,
sustained real-time inference, 4K mask quality, and difficult photographic edges
are not qualified by these checks.

Portrait source: the public-domain official White House photograph by Pete Souza,
available on [Wikimedia Commons](https://commons.wikimedia.org/wiki/File:President_Barack_Obama.jpg).
