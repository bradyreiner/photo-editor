# Gaussian thin-lens rendering: first optical implementation

Later checkpoint: explicit still-photo exports now use tiled rendering up to
8192 pixels per edge; the 4096 optical limit described below remains for texture
previews and film-video output. See `gaussian-tiled-photo-export-2026-09-09.md`.
This supersedes the earlier still-photo limit, not the calibration limitations.

The later progressive-preview checkpoint also supersedes the final-image-only
publication behavior below: settled previews can now display immutable aperture
prefixes with film effects. See `gaussian-progressive-optics-2026-09-09.md`.

The immersive viewer now has a finite-aperture rendering path, separate from
generic photo tone controls. This advances the first selected film/lens target;
it does not implement Portra color/grain, halation, a calibrated 50mm f/1.4 lens,
or the other four film profiles.

## Implemented

Focus distance and aperture radius are explicit **scene units**. The capture
reconstruction has no calibrated metre scale, so physical f-numbers/metres are
not invented. Aperture radius zero is the original pinhole path. The UI offers
circular and 6–9-blade apertures; the validated model also supports 3–16 blades.
Blade rotation, roundness and 16/32/64/128 sample quality persist with the shot.
Focus distance has a manual control and a clearly labeled capture-depth estimate;
the estimate is not click-to-focus or an autofocus solution.

The aperture integral uses deterministic, prefix-stable, area-distributed disk
samples, with rejection against the rounded polygon boundary. Each sample shifts
the camera on its aperture plane and offsets the principal point so that all
views agree at the focus plane. Projection displacement is
`focalPixels * lensOffset * (1 / focusDistance - 1 / pointDepth)`. This is a
[thin-lens camera model](https://www.pbr-book.org/4ed/Cameras_and_Film/Projective_Camera_Models),
not a named multi-element lens or a post-render Gaussian blur.

Gaussian footprints, appearance and occlusion are reevaluated for each aperture
view. Offsets are perpendicular to the forward axis, so camera-space depth and
the exact sorted order are shared across samples. Each fully composited sample
is decoded from reconstructed sRGB into linearized RGB, accumulated in float32,
then processed by the common exposure/look/output pass. This is not recovered
scene radiance or spectral light: source tone mapping and clipped highlights
remain limitations of the trained capture.

The integral is normalized to preserve exposure as aperture size changes; it is
not yet coupled to physical aperture throughput, shutter speed or ISO. Focus
breathing and other lens-specific optical behavior are not modeled.

## Interaction, resources and persistence

- Movement uses a bounded pinhole draft. After settling, the worker integrates
  the selected aperture samples. The UI shows progress and publishes the final
  immutable photo texture; it does not yet publish intermediate optical images.
- New requests cancel superseded settled renders. Cancellation is checked during
  depth setup, between radix passes and between GPU samples. The current GPU
  command must finish; it is not forcibly interrupted. Hidden/source-switched/
  destroyed viewports cancel their preview work without canceling accepted photos.
  A canceled refinement is neither a render error nor a published empty frame.
- Photo exports retain the existing frozen camera/time/settings and serialized
  renderer ownership. Accepted exports may still delay preview rendering on that
  renderer. Film export now passes its cancellation token into optical rendering.
- Optical output is bounded to 4096 pixels per edge. A larger optical request
  fails explicitly. The pinhole path retains its existing 8192 limit. Optical
  integration uses one extra float32 texture, not one retained texture per sample;
  high-resolution peak memory still needs representative stress measurement.
- `framehouse-gaussian-photo-settings-v2` carries `framehouse-thin-lens-v1`.
  Defaults preserve v1 settings and neutral camera fingerprints. Saved-view and
  photo-receipt containers remain v2 for adjusted shots. Older parsers reject the
  unsupported settings version instead of silently dropping optics. Camera paths
  interpolate focus distance and aperture radius; shape/rotation/roundness/quality
  must agree between their shots.
- Photo settings use compact Tone, Focus and Aperture pages with scroll fallback.
  Focus changes are committed only after user editing, not when rounded display
  text merely loses focus.

## Evidence and remaining gates

The native test coverage includes deterministic aperture sampling/rotation,
disk moments, analytic focal-plane invariance and front/back defocus direction,
a focused thin Gaussian, and an occluded emitter revealed by aperture views.
Every pixel is compared against an independently accumulated sequence of
explicit pinhole renders (within two 8-bit values for reference quantization).
Actual preview-texture readback matches optical export exactly. Tests also cover
neutral identity, pinhole draft behavior, cancellation during and before an
optical render, output limits, settings validation/roundtrip, path interpolation
and resuming a viewport after a canceled refinement.

First native UI proof: `/private/tmp/framehouse-thin-lens-wUwyID/proof/receipt.json`
and `ui/20260909T180907.916777Z-perf-workspace-switch-7f49176a` under that root.
The existing 753,038-splat reconstruction renders a 16-sample, seven-blade photo;
the camera/settings survive the UI workflow and two PNG exports match exactly:
`586d8d60d9acef0f7322aa4a11c154dc6ec4be31d4957ceb971652c894da58e0`.
Foreground eligibility and all functional proof flags pass. Neutral and adjusted
pinhole exports retain the preceding checkpoint hashes. The first controls and
photo captures were inspected; the overly tall panel prompted the compact-page
UI revision. Six focused tests pass in the first expanded run.

Final proof after the compact controls and focus-commit correction:
`/private/tmp/framehouse-thin-lens-wUwyID/final-proof/receipt.json`, with wrapper
`final-ui/20260909T182134.663185Z-perf-workspace-switch-cd266f90` under that root.
Foreground eligibility and immersive, lens, photo-settings and rapid-navigation
checks all pass. Focus and aperture settings survive page switching without a
rounded focus-field commit; the repeated optical PNG retains the exact hash
above. The 800-by-596 JPEG is tagged sRGB and measures 47.7956 dB PSNR against the
same optical PNG. This verifies lossy-codec consistency, not reconstruction or
photographic accuracy. Compact Focus/Aperture pages and the refinement-progress
capture were visually inspected. The final native run retains existing Export
warnings, with no observed Scene errors.

All six focused native tests pass in the final sequential run (4.33 seconds):
`gaussian_scene_render_test`, `gaussian_spacetime_render_test`,
`gaussian_film_export_test`, `gaussian_saved_views_test`,
`gaussian_camera_path_test` and `gaussian_viewport_handoff_test`.

Still open: click-to-focus, scene-scale/sensor calibration, named-lens aberrations
and cat-eye clipping, high-quality sampling/convergence on strong point highlights,
shutter integration, grain, halation and measured film response. Existing scene
ghosting/missing geometry is unchanged. No sustained-realtime, SOTA, calibrated
film-stock or full-goal completion claim is made. The numerical and synthetic
optical tests are not a replacement for real lens/film reference photography.

The user's app process/bundle, scene data, original photographs, model weights and
catalog authority were not replaced or modified. Native proof uses an isolated
test application and new temporary images/receipts. Existing Timeline/Export QML
warnings were observed separately; no Scene binding error was observed in the
first native run.
