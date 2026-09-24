# Gaussian photographic controls: first connected pass

This continues the immersive viewer, not completion of physical camera or film
emulation. Scene now has a nonmodal Photo settings card that leaves the 3D view
interactive. Lens framing, exposure compensation and a tone look feed the native
GPU renderer in both live and settled modes. The card closes when leaving Scene.

## Rendering contract

- Lens framing is 0.25–4× the calibrated capture intrinsics. It changes focal
  projection and splat covariance together, preserving principal point, aspect,
  camera pose and navigation pivot. It is deliberately not labeled millimeters:
  a sensor size has not been calibrated.
- Exposure is −6–6 EV on the reconstructed photograph. Accumulation remains in
  the display-referred domain used by training. The final pass decodes sRGB,
  multiplies linearized RGB by `2^EV`, applies the optional look, and re-encodes.
  The transfer follows the [ICC sRGB registry](https://registry.color.org/rgb-registry/srgb).
  This is not recovered sensor exposure; clipped source highlights stay lost.
- Neutral is an exact bypass. Soft highlights uses a generic smooth shoulder
  above peak linear RGB 0.6, scaling all channels together. Monochrome uses linear
  sRGB luminance followed by the same shoulder. These are creative looks, not
  measured film stocks, spectral response, grain, halation or physical optics.
- Preview stays texture-only; no production CPU readback was added. Export uses
  the same shader, frozen camera/settings/time and existing no-overwrite staging.
  Exported PNG/JPG images remain tagged sRGB.

The missing optical pass is substantive: focus distance/scene scale, aperture
sampling and occlusion-correct depth of field, temporal shutter integration,
sensor/ISO behavior and measured film response still require implementation and
real photographic validation. A lens model requires more than a tone curve; see
the [PBRT camera models](https://www.pbr-book.org/4ed/Cameras_and_Film/Projective_Camera_Models).

## Persistence and compatibility

`GaussianCamera` owns versioned photo settings, so saved shots, camera-path frames
and accepted exports cannot drift when the controls change. Camera descriptions
omit default settings, preserving the existing calibrated-scene fingerprint and
neutral output. Old saved views load as neutral. Invalid types, nonfinite values,
unknown looks/versions/fields and out-of-range settings are rejected.

Adjusted saved-shot stores use `framehouse-gaussian-views-v2`; adjusted photo
receipts use `framehouse-gaussian-photo-v2`. Neutral-only stores/receipts retain
v1. Older builds reject v2 stores instead of silently discarding the new settings.
The view writer retains its lock, bounds and atomic replacement behavior.
Camera paths interpolate exposure in stops and focal scale. All shots in a path
must share one look; a mixed-look path is rejected rather than making hidden cuts.

## Evidence

Nine focused tests pass: scene rendering, spacetime rendering, film export,
saved views, camera paths, viewport handoff, scene controller, foreground rules
and workspace comparison. The native test driver builds; `git diff --check` passes.
An existing QString range-copy warning in the viewport test remains unrelated.

- 18 independent double-precision exposure/look checks agree with Metal to at
  most one 8-bit code value at the analytic splat center. Ten additional checks
  cover black, the sRGB toe/knee, and over-white values before final clipping.
- Test-only readback compares every pixel of the actual published GPU preview
  texture with explicit export at identical dimensions/settings for all 18
  combinations. Neutral output remains exact; lens framing changes projection.
- Saved-shot settings roundtrip exactly. Validation rejects malformed settings;
  path tests cover settings interpolation and mixed-look rejection. Hidden-pane
  tests preserve navigation pivot through a photo adjustment. In-flight exports
  retain their old settings while the preview advances to the new ones.
- The first actual Main.qml run against the existing 753,038-splat scene passes
  photo controls, full-window geometry, rapid navigation, camera preservation,
  and exact repeat exports after page switching. Its foreground eligibility is
  false, so it is functional evidence only, not accepted performance evidence.

Evidence root: `/private/tmp/framehouse-gaussian-photo-iewVTE`.
First run: `proof/receipt.json` and
`ui/20260909T171621.364937Z-perf-workspace-switch-3868730d`.
Neutral photo SHA-256 remains the pre-redesign value:
`21e3bcb5f06e6093e6a18fb3e3198ddf0d96921fe595643a2fe73ae625df8abb`.
Adjusted 1.25× / +0.75 EV / Soft highlights output:
`b991d091c3b09755ed862b46a543b0071be6c4aa9f9f47d9b159e0418d0ba7a7`.
The controls screenshot was visually inspected. Existing Export preflight QML
warnings remain separate; no Scene binding errors were observed.

Final run: `final-proof/receipt.json` and
`final-ui/20260909T171949.919647Z-perf-workspace-switch-1da527f5`.
It has `eligible: true`, an empty error, and passing immersive, rapid-navigation,
camera-preservation and photographic-control checks. It additionally verifies
the nonmodal panel closes when leaving Scene and the adjusted export receipt is
v2. Both photo hashes above remain exact. The final settings-card and resolved
photo screenshots were visually inspected. This qualifies the native functional
run, not a speedup or a sustained interactive framerate claim.

No training, model download, catalog-authority change, source-image change or
replacement/restart of the user's running app was performed. No 60 fps,
reconstruction-quality, all-color-spaces, or full-goal completion claim is made.
