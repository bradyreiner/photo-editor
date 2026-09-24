# Aperture-offset precision fix

The native renderer previously subtracted each tiny aperture offset from the
large translation column of its world-to-camera matrix. Float rounding could
discard or quantize that shift, while the principal-point compensation still
used the requested offset. Rays then failed to meet on the intended focus plane.
The SH viewing direction also used an aperture offset added to an absolute
world-space camera position, exposing it to the same rounding problem.

## Reproduction and fix

The native fixture uses exactly representable target/camera translations so
source-coordinate quantization is not being mistaken for a rendering defect.
It renders an isolated thin Gaussian on the focus plane with a circular pupil
and 128 samples, then compares against the pinhole image.

| Translation along world X | Before: maximum focused/pinhole code error | After |
| --- | --- | --- |
| 0 | 0/255 | 0/255 |
| 1,000,000 | 87/255 | 0/255 |
| 10,000,000 | 183/255 | 0/255 |

At the largest translation the focused center fell from 255 to 72 and the image
became a horizontal smear. After the fix, the focused image equals pinhole at
every pixel. Before/after images were visually inspected.

- Before: `/private/tmp/framehouse-optical-precision-before-20260909/receipt.json`
- Expanded final: `/private/tmp/framehouse-optical-precision-final-20260909/receipt.json`

The renderer now sends camera-plane aperture offsets separately. The GPU first
evaluates the shared base view transform, then subtracts the small transverse
offset. The depth coordinate remains unchanged across all aperture samples,
preserving the single depth sort. For SH, it subtracts the base camera position
first and the separate world-space pupil offset second. No scene buffers are
rebased or copied per aperture sample; no denoising or sharpening masks the error.

## Verification

The expanded regression additionally checks degree-three spatial SH appearance
and the temporal covariance-factor path (at its mean capture time). Their output
pixels match the corresponding origin render exactly at both translated camera
positions. This does not cover arbitrary temporal velocities or precision of
large absolute time/position combinations.

Seven focused tests pass in 33.35 seconds: rendering (including optical precision,
high-sample bokeh, progressive prefixes and tiled output), physical-camera images,
temporal rendering, viewport handoff, saved views, paths and film export.

All thirty baseline SUV pinhole motion-frame hashes are exact before/after,
with the same scene artifact hash:

- `/private/tmp/framehouse-optical-precision-motion-before-20260909.json`
- `/private/tmp/framehouse-optical-precision-motion-after-20260909.json`

Individual before/after timings were taken under concurrent training and are not
a speedup claim. Pixel preservation, not speed, is the acceptance criterion.

Full-viewer verification is tracked separately from the image tests. The first
run waited for a foreground window before loading the scene; it is not counted
as a successful UI proof. The test harness now bounds this gate to five seconds
and records a foreground failure explicitly instead of continuing after a long
wait as though the gate passed. It also exits unsuccessfully if foreground
eligibility is lost later, even when functional checks pass.

The first run's `/private/tmp/framehouse-optical-precision-ui-20260909/receipt.json`
records functional `passed: true` but `eligible: false`; it must not support
foreground performance claims. A fresh run after other native tests finished
failed the strict gate with `active: false`, `exposed: true`, application state 2:
`/private/tmp/framehouse-optical-precision-ui-final-20260909/foreground-failure.json`.
Its screenshot was inspected. Fresh foreground UI verification remains open;
the native image/renderer proofs above are independently successful. No training
process was stopped or restarted to resolve this UI-environment condition.

## Remaining precision/fidelity boundary

This fixes loss of aperture offsets in rendering. It does not recover detail
already lost in float scene coordinates, eliminate cancellation in every base
world-to-camera transform, or make the CPU `gaussian_aperture_camera` helper's
absolute float position exact at arbitrary scales. The earlier analytical
physical-camera world-position limitation is therefore not fully closed.

The baseline SUV's reconstruction streaks and soft geometry remain separate.
Physical-camera UI/persistence, measured scene scale, RAW/radiance, lens-specific
optics and measured film response also remain open. The installed app, originals,
baseline project and live high-detail training were not replaced.
