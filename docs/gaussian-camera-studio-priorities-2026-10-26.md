# Physical camera and Scene studio priorities

The user has paused the broad reference-video parity goal and redirected the
current work to photographing splats with realistic, customizable camera/lens
behavior, a studio Scene workspace, and interactive or highly performant output.
Grain and halation are explicit requirements. The prior spatial-lighting
performance experiment was stopped at the user's pause; no optimization from
that unfinished loop is accepted.

## Required result

- A physically dimensioned camera: sensor/gate dimensions, output crop, actual
  field of view, scene scale, focusing and an inspectable focus plane.
- Compound optical prescriptions with rays intersecting glass interfaces and
  refracting through them. Refraction, aperture clipping and transmission must
  cause the resulting focus, bokeh, vignetting and distortion. Artistic aberration
  sliders alone do not establish this requirement.
- Editable lens, iris and recording-medium parameters. Do not claim a named
  commercial camera/lens or film stock is matched without measured data.
- Grain with consistent physical scale and exposure-dependent statistics, plus
  film halation motivated by transmission and reflection in the recording medium.
  Digital sensor noise and film grain are distinct models.
- Persistent studio panels for scene contents, camera/lens, lighting, film/look
  and shots/path controls, using Timeline's workspace hierarchy and shared visual
  controls. Avoid stacking floating settings panels over the photograph.
- GPU/progressive rendering with bounded work, cancellation and visible progress.
  Qualify image fidelity and interaction separately. A fast approximate preview
  must converge to the same configured optical model for the resolved photo.

## Current evidence and boundary

The physical camera implements an ideal thin lens, finite-focus image distance,
an explicit user-supplied scale and a cropped sensor gate. The inspector now
reports actual field of view, pupil diameter, lens/sensor focus distances and
depth of field for an explicit permissible blur criterion. Refocusing retains
the authored gate across rounded preview dimensions. Analytic and native image
tests cover these behaviors.

Scene now has persistent scene contents, a central photograph, a resizable
inspector and a shots/path deck. Native pointer/keyboard checks cover physical
camera controls, compact nonoverlapping geometry, saved-shot recall and
workspace recreation. The packaged application has been captured with the real
Bonsai scene. This establishes layout/workflow behavior, not foreground latency.

The current photographic example is now a real human in a merged 3D forest,
with five native 4K photographs, physical 85–135 mm zoom, manual background
focus, a saved zoom/focus path and a delivered movie preview. Native physical
controls preserve camera pose after orbiting. Full forest-image comparisons are
pixel exact after the renderer changes. The 2.19-million-point scene still falls
short of the 30 fps interaction target; full 4K stills take seconds to finish.
See [forest portrait implementation and evidence](gaussian-forest-portrait-2026-10-26.md).

An independent double-precision compound-lens reference and its Metal counterpart
now trace spherical glass interfaces, Snell refraction, Fresnel transmission,
absorption, iris clipping and wavelength-dependent glass. Across 73,728 sampled
rays the GPU matched clipping and stayed below 0.0045 projected pixel error at
8K. The compound model is not yet connected to photographic splat capture;
existing rendered photographs still use the ideal/creative lens renderer.

Grain and halation now have an opt-in film-plane mode, with micron dimensions,
pixel-integrated statistical density grain and RGB linear-light diffusion.
Original creative film recipes retain their rendering. This is a generic
statistical approximation, not measured emulsion chemistry or a film-stock match.
See [film-plane implementation and evidence](gaussian-film-plane-2026-10-26.md).

The remaining major gates are compound-ray scene integration and editable lens
prescriptions, measured recording-medium profiles, and integrated performance.
The combined film pass on a cached synthetic 1536 x 1024 source measured about
2.3 ms GPU time. Full Bonsai beauty renders still took 31-33 seconds with 64
aperture / 128 lighting samples, six tiles and shadowed haze; their tiled optical
sources were rerendered. These timings do not establish realtime photography.

## Research sources and implementation direction

- [PBRT: Realistic Cameras](https://www.pbr-book.org/3ed-2018/Camera_Models/Realistic_Cameras)
  and its [current implementation](https://github.com/mmp/pbrt-v4/blob/master/src/pbrt/cameras.cpp):
  surface prescriptions, Snell refraction, film focusing and exit-pupil sampling.
- [Published double-Gauss prescription distributed with PBRT](https://raw.githubusercontent.com/mmp/pbrt-v4-scenes/master/lenses/dgauss.50mm.dat):
  numeric reference design attributed to Tronnier's US patent 2,673,491, scaled to
  50 mm. It is a generic optical design, not a modern branded-lens match.
- [Polynomial Optics](https://jo.dreggn.org/home/2012_polynomial_optics.pdf):
  evaluate acceleration only against a reference glass trace; an approximate ray
  map needs a quantified validity range and image-error checks.
- [Physically-Based Real-Time Lens Flare Rendering](https://resources.mpi-inf.mpg.de/lensflareRendering/):
  reference for surface-based optical transport and later reflection/ghost work.
- [Realistic Film Grain Rendering](https://www.ipol.im/pub/art/2017/192/):
  stochastic, resolution-independent grain research. Consult the paper rather
  than importing its GPL implementation into the application.
- [Image Permanence Institute: anti-halation backing](https://www.filmcare.org/vd_antihalation.php):
  light returning through the emulsion and the role of backing absorption.

The implementation starts with a double-precision optical reference and
independent checks for intersections, refraction, total internal reflection,
optical reversibility, aperture clipping and paraxial cardinal properties.
GPU camera rays and photographic capture must then consume that prescription;
a standalone optics test is not completion. Scene studio and medium controls
must be exercised in the actual native application before UI completion claims.
