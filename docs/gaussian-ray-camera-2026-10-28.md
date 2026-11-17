# Scene camera optics and controls

The camera inspector presents focus, focal length/framing, aperture and exposure
as four large, quietly styled cards with prominent editable values, fine steppers
and monochrome photography icons. Icon actions handle autofocus, subject picking, reset and
advanced settings. Ray quality, pupil softness, bloom, halation, lens/film looks,
color and detailed rendering controls remain behind Advanced settings. Contents
and Shots start collapsed; their toolbar toggles retain access. A one-time layout
migration also simplifies older saved layouts. Uncalibrated
scenes keep their honest scene-unit controls; sensor dimensions and measured
scene scale enable millimetres, metres and f-stops.

Camera adjustments now retain optical depth of field and film processing in the
live preview instead of temporarily switching to a pinhole. The preview caps
its long edge at 720 pixels, pupil samples at 32 and sampled lighting at 16.
The authored camera settings remain intact and are used for settled rendering,
saved shots and exports. Orbiting and panning retain the cheaper navigation draft.
Color and film edits continue to reuse the completed optical source.

## Camera movement

Orbit and pan are explicit viewport modes. Pick an orbit target or double-click
a surface to choose the point to rotate around, without recentering the image or
changing optical focus. A normal focus pick also establishes a subject pivot.
Panning uses the current camera axes and the field of view at the pivot depth,
so screen movement stays predictable after rotation or lens changes. Pan and
dolly rebase the current pose rather than reapplying movement in the original
capture camera's axes. Reset restores the capture camera and clears the pivot.
The navigation pivot is temporary UI state; saved shots retain the resulting pose.
Shift-drag, middle-drag and right-drag pan directly. Arrow/WASD keys translate,
Q/E dolly, F arms target picking and R resets. The Pan HUD exposes hold-to-repeat
direction and dolly buttons while keeping the default Orbit HUD compact.

## Optical model

The new opt-in lens schema `framehouse-thin-lens-v7` stores `rayTraced` and
`pupilSoftness`, alongside the existing ideal lens and pupil parameters. Old
records remain readable and keep their original rendering model. Camera paths
require matching ray modes and pupil softness at their endpoints.

Primary-ray capture samples the pixel footprint and the finite pupil, aims each
ray through its point on the focus plane, and evaluates the anisotropic Gaussian
field in depth order. Samples accumulate linear light. This replaces repeated
screen-space projections in the new mode and decorrelates their coherent pupil
patterns. Quality controls trade additional samples for lower integration noise.

Pupil softness models radial transmission `(1-r*r)^softness`. The aperture is
still geometrical; its transmission now tapers before the edge. The integral is
normalized for this editor's independent exposure control. Barrel-blocked rays
remain dark contributions; normalization must not remove vignetting.

Bloom and halation run on the resolved optical image before the display
transform. A cutoff above the available highlight luminance intentionally gives
no glow. The main controls provide usable starting cutoffs when first enabling
the effects, while the advanced controls retain exact values and independent
spread, return color and highlight selection.

This remains an ideal thin lens, reconstructed Gaussian appearance/opacity and
an approximate film diffusion model. It does not recover scene radiometry from
captured RGB, simulate diffraction or calibrated emulsion, or connect the
standalone compound-glass tracer to photographic capture. Ray tracing alone does
not establish a measured real lens match. The distinction between ideal lens
sampling and multi-element refraction follows the [PBRT camera models](https://www.pbr-book.org/4ed/Cameras_and_Film/Projective_Camera_Models)
and [realistic camera chapter](https://pbr-book.org/3ed-2018/Camera_Models/Realistic_Cameras).

## Verification

Eight focused suites pass: camera navigation, viewport handoff, legacy rendering,
ray-camera optics, ray-scene integration, saved shots, camera paths, and native
focus controls. The
legacy renderer suite includes independent lens/film/tone references, autofocus,
source-cache invalidation, translation, tiled export and cancellation. New ray
scene checks exercise camera-visible emitters, spatial reflection/environment,
per-segment haze, immutable progressive textures, and the 400 by 400 batch-boundary
regression.

The complete isolated Cocoa/Metal studio workflow also passes with real pointer
and keyboard input. It exercises the camera cards and Advanced ray/glow controls,
subject orbit/pan/navigation, calibrated sensor and f-number edits, advanced
film/color controls, saved-shot recall, compact layout and workspace recreation.
Screenshots and its receipt are in `native-quiet/`. The proof seeds an older
expanded layout and verifies migration to collapsed Contents and Shots, then uses
their native toggle buttons to reopen and close each panel. It checks that the
four large cards stay visible while secondary controls start hidden, open through
Advanced, and hide again. The final focused run completed eight tests in 18.56
seconds; it also checks restoring a saved
ray-traced recipe after locally switching ray tracing off. These are native
Scene component checks, not a complete production-app startup or latency audit.

The Release desktop app was rebuilt successfully with the final camera QML.
The generated camera-layout QML caches are newer than their inputs and older than
the linked app. `verification.json` records source and binary SHA-256 values,
the test logs, and a final native follow-up after a shared icon-file edit.
An unrelated final-link failure required registering the existing Timeline
frame-quad animation implementation in the desktop app target.

The isolated optical proof measures an 11.2 px predicted blur radius with added
variance 31.61 px squared (predicted 31.36). Softness 2 measures 15.98 px squared
(predicted 15.68). Interior bokeh variation falls from 0.715 at 32 samples to
0.087 at 512 samples; integrated energy changes by 5.2%. The source cutoff and
8-bit output contribute finite integration/quantization error. These are
synthetic ideal-model measurements, not measured lens calibration.

The existing public office capture (1,105,897 Gaussians) rendered at 512 by 341,
64 rays per pixel and softness 1 in 4.89 seconds. A subsequent halation-only edit
reused the resolved source, tracing zero new primary rays, in 1.26 milliseconds.
This is one bounded example, not a realtime or 4K performance claim.

Receipts and images are under
[evidence/gaussian-ray-camera-2026-10-28](evidence/gaussian-ray-camera-2026-10-28).
Photographic calibration and large-scene foreground latency remain separate
qualification work.
