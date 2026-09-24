# Physical thin-lens conversion: foundation, not a calibrated camera release

The next layer of the photographic-quality goal now has an executable physical
camera conversion kernel in `apps/desktop/src/gaussian_physical_camera.h` and
independent analytical/native-image tests. It is not yet wired into the viewer's
controls or persisted as physical calibration with saved shots. Existing scenes,
camera fingerprints, renderer defaults and the installed app are unchanged.

## Contract

An explicit `metresPerSceneUnit` is required; its default is invalid. Never infer
measured scene scale from arbitrary COLMAP coordinates. Inputs include an
explicitly oriented film gate (default 36 x 24 mm), focal length, f-number and
focus distance measured from the ideal lens plane. Portrait full-frame uses
24 x 36 mm. Output-aspect mismatch center-crops the gate instead of stretching
the image or silently rotating the sensor. Output remains bounded to 8192/edge.

For focal length f, object focus S, and f-number N:

- Image distance v = f*S/(S-f); focus must be beyond f.
- Entrance pupil diameter D = f/N; scene-unit aperture radius is D/(2*scale).
- Projection focal pixels use v and the effective cropped gate, not f alone.
  This includes ideal unit-focus magnification/breathing at finite distances.
- Camera pose, exposure/look, aperture shape/sample quality and creative film
  settings are preserved. Framing is replaced by the new physical projection.
- A strict versioned input parser rejects unknown/missing fields, string numbers,
  invalid scales, nonfinite values and invalid optical ranges. A conversion also
  validates pose orthonormality and the renderer's scene-unit constraints.

The returned calibration receipt describes its normalized-aperture exposure
model explicitly. F-number changes geometric defocus but does not yet model
absolute film exposure, T-stops, shutter, ISO, bellows-factor exposure, cos-fourth
falloff, diffraction, pupil magnification, cat-eye clipping, spectral aberrations
or a multi-element lens prescription. The source remains reconstructed
display-referred imagery. This is not a named-lens or film-stock match.

The thin-lens and pupil-ray distinction follows
[PBRT's projective cameras](https://www.pbr-book.org/4ed/Cameras_and_Film/Projective_Camera_Models)
and [realistic lens systems](https://www.pbr-book.org/3ed-2018/Camera_Models/Realistic_Cameras).
The conversion equations and tests are implemented locally, not copied source.

## Evidence

`gaussian_physical_camera_test` verifies physical-input roundtrip/rejection,
preserved pose/photo fields, sensor cropping/orientation, resolution invariance,
and 576 analytical signed-defocus cases across 28/35/50/85 mm, four apertures,
four focus distances, three scene scales and three target depths. The reference
uses the independent film-plane expression D*(1-v/v_z), sign-reversed for the
renderer’s upright virtual image. The analytical fixture uses a local origin to
isolate lens math from absolute-position float cancellation.

The native Metal proof uses three finite Gaussian targets at 0.5, 1 and 2 m,
a 50 mm f/1.4 lens focused at 1 m, a 36 x 24 mm gate and 128 aperture samples:

`/private/tmp/framehouse-physical-camera-proof-20260909-final/receipt.json`

The final five-test run passed in 5.86 seconds: physical-camera math, physical
Metal images, existing Gaussian rendering, focus picking and saved views. The
proof was then rerun with the tightened gates and its final receipt saved.
`git diff --check` passes. Only native test targets were built.

- Focused plane maximum difference from pinhole: **0/255**.
- Defocus variance, near: predicted 60.805062, measured 60.713401 pixels squared.
- Defocus variance, far: predicted 15.201266, measured 15.176226 pixels squared.
- Doubling f-number gives variance ratio **0.249965**, expected 0.25.
- Doubling output dimensions gives variance ratio **3.998895**, expected 4.
- Changing scene coordinates by 100x with reciprocal physical scale gives
  **0/255** maximum difference. GPU texture/readback photo pixels agree exactly.

The 600 x 400 and 1200 x 800 outputs were inspected. Focus/defocus ordering is
correct, but isolated-emitter blur still reveals the finite aperture-sampling
pattern at 128 samples. Blur-size agreement is not proof of fully converged,
smooth bokeh. The final test gates were tightened from the initial exploratory
tolerances to 1% defocus variance (0.02 pixel-squared floor), 0.003 absolute
f-number variance-ratio error and 0.04 resolution-ratio error.

An initial analytical fixture at world position (2,3,4), 10 metres/scene-unit,
85 mm f/2, focus 0.3 m and target 0.15 m exceeded a 0.06-pixel ray-diameter
tolerance (approximately -2800.45 vs -2800.39 pixels). Float aperture-origin
addition limits this test; moving the analytical fixture to the origin isolates
the physical conversion but does not fix world-coordinate precision. Keep a
camera-relative precision gate open before claiming arbitrary-scale sharpness.

## Next required integration

1. Persist physical inputs and scale provenance with saved shots. Re-resolve
   projection when focus or focal length changes; do not merely relabel current
   scene-unit controls as calibrated millimetres, metres and f-stops.
2. Add explicit viewer calibration controls and validate actual UI, focus picks,
   camera paths, reopen and frozen export behavior. No measured SUV scale has
   been supplied, so do not silently apply the test's one-metre-per-unit scale.
3. Measure and repair camera-relative precision, then test smooth point-emitter
   bokeh convergence, occluding geometry and reference lens photographs.
4. Keep sharp neutral reconstruction, calibrated RAW/radiance and measured film
   response as separate requirements. This kernel does not close those gates.
