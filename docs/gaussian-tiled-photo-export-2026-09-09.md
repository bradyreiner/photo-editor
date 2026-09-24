# Tiled high-resolution Gaussian photographs

Explicit still-photo export now supports up to 8192 pixels per edge with the
existing lens, grain and halation settings. This removes the former 4096-edge
optical-photo rejection; it does not recover missing reconstruction detail.
The production app bundle has not been rebuilt or replaced for this checkpoint.

## Implementation

- Readback exports above 4096 use 2048-pixel core tiles. Texture-only optical/film
  previews and film-video exports keep their existing 4096 limits.
- Every tile uses the full camera projection, full-canvas effect scale and global
  grain coordinates. Halation extends each tile by its three-sigma support before
  copying only the core. Lens integration remains the same per-aperture renderer.
- Tiles share a single exact depth order for the immutable scene/camera/time.
  Tile-local resolved sources never enter the whole-frame preview cache.
- Only bounded tile textures and one full CPU output image are retained, not an
  oversized final GPU texture or one image per aperture sample. The maximum-size
  CPU output alone is 256 MiB; full-app maximum-size memory stress remains open.
- Cancellation is checked between tiles and aperture samples; a canceled photo
  returns no partial image. Renderer-level cancellation is tested, but no new
  user-facing photo-cancel button is implemented. Accepted photo exports still
  share serialized renderer ownership and can delay camera previews.
- Export controls offer 6000 and 8192 alongside existing sizes; the viewer shows
  completed/total tiles and records the completed export's render metrics.

## Proof

Artifacts: `/private/tmp/framehouse-tiled-photo-ugZCYT/` (temporary local evidence).

- Six focused native tests pass, 5.86 seconds: scene renderer, space-time renderer,
  film export, saved views, camera paths and viewport handoff. The scene test adds
  tiled/full-image comparisons for neutral, grain, halation, finite aperture,
  combined settings and temporal scenes, including off-center intrinsics and
  bright emitters across tile boundaries. Maximum channel difference is 1/255.
- Repeated tiled output is exact. A 6000 × 4000 synthetic lens/grain image uses
  six tiles and 96 total aperture renders; mid-photo cancellation publishes no
  partial output. Tile admission, color tags and cache isolation are checked.
- `preview-parity.json`: all thirty SUV raw preview pixel hashes exactly match
  `/private/tmp/framehouse-viewer-gpu-perf-ghHbUi/baseline.json`. Timings here are
  contended by training and are not a performance-promotion claim.
- `ui-final/receipt.json`: actual Main.qml viewer proof is eligible and passed.
  The real export selector supplies 6000. The resulting sRGB PNG is 4499 × 6000
  (26.994 MP), with 16 aperture samples, seven blades, grain amount 0.15 and
  halation amount 0.3. Nine tiles use a 36-pixel halo, at most 4,494,400 pixels per
  tile and 144 aperture renders total. GPU time is 11,277.72 ms; renderer time is
  11,394.44 ms; end-to-end export is 11,896 ms. The photo receipt matches the
  requested camera/settings, and an existing destination is rejected.
- Visually inspected `ui-final/high-resolution-photo-overview.png`,
  `tile-junction-upper-100pct.png`, `tile-junction-lower-100pct.png` and
  `photo-export-progress.png`. The two 400 × 400 crops straddle tile junctions;
  no obvious seams appear. Existing geometric streaks around the mirror and
  windshield, soft bodywork and silhouette defects remain clearly visible.

## Remaining fidelity and UX limits

The real photo proof uses the old 1600-edge, million-splat SUV reconstruction,
not the still-training higher-detail candidate. More output pixels are not more
recovered scene information. Current output is sRGB/display-referred; aperture
radius and focus use scene units, not measured full-frame lens calibration.
Grain and halation remain creative approximations, not calibrated film scans.
Neither arbitrary-view photographic equivalence nor realtime high-resolution
optical export is established. The 8192 limit is implemented, but only 24 MP
synthetic and approximately 27 MP actual-photo output are validated here.
