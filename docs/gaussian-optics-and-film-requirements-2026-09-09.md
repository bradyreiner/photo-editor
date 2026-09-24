# Gaussian optics and film: required end state

The user explicitly requires grain, halation and accurate bokeh in the immersive
Gaussian photographic viewer. This is the implementation/acceptance contract,
not an implementation-complete report. The current renderer has focal scaling,
exposure compensation, generic tone looks and a first finite-aperture thin-lens
implementation in scene units. See `gaussian-thin-lens-2026-09-09.md` for its
mathematical/runtime proof and limitations. Creative grain and halation are now
connected to settled preview and export; see `gaussian-film-effects-2026-09-09.md`.
They are not measured emulsion/spectral models. The complete film and
bokeh/calibration gates below remain open.

## Required image formation

- [ ] Bokeh: finite-aperture sampling with a focus plane, foreground/background
  defocus and visibility evaluated per aperture sample. Include aperture size,
  blade count, blade rotation and roundness. Focus picking must select a useful
  scene surface, with a visible focus-distance control. A generic thin-lens model
  must be labeled as such. Cat-eye clipping, aberrations and named-lens matching
  require a lens-specific model/profile, not a radial blur mask. Physical f-number
  and distance labels require defined sensor geometry and scene scale; do not
  invent meters for a scale-ambiguous reconstruction. The distinction between
  finite-aperture and full lens-system models follows
  [PBRT's camera model](https://www.pbr-book.org/4ed/Cameras_and_Film/Projective_Camera_Models)
  and [realistic lens model](https://www.pbr-book.org/3ed-2018/Camera_Models/Realistic_Cameras).
- [ ] Halation: an exposure-dependent, spatially spreading film-layer response
  with controllable radius, strength and spectral/color behavior. Keep it
  separate from lens bloom and glow. A named stock needs reference calibration;
  anti-halation construction differs between products, as documented by
  [Kodak](https://www.kodak.com/en/motion/product/post/print-films/vision-color-2383-3383/features/).
- [ ] Grain: exposure/density-dependent structure with controls for size, amount
  and color correlation. Use a persisted shot seed, stable film-plane coordinates
  and appropriate filtering at preview/export sizes. A still must not flicker on
  idle refinement or returning to a page. Movie frames need an explicit temporal
  grain policy. Independent channel white noise is not a calibrated film model.

Optical aperture/shutter integration precedes the film response. Halation belongs
in the film exposure model, and grain belongs in the emulsion/density model; both
must be designed with the response curve rather than tacked onto clipped output.
The current reconstruction is display-referred sRGB, not calibrated scene
radiance. Decoding its transfer function does not recover clipped highlights,
spectral information or source exposure. Any resulting emulation must disclose
that limitation; absolute photographic accuracy needs a calibrated capture path.

## Interaction and proof gates

- [ ] Movement remains responsive with a bounded draft. When movement stops,
  progressively converge the lens/film render. New input cancels superseded
  expensive work; a low-priority refinement cannot hold navigation behind it.
- [ ] All optical/film parameters, seed and model/profile versions persist with
  the shot and are frozen for accepted exports. Neutral settings retain current
  output. Same-size settled preview and export agree; different-size outputs
  preserve the physical scale and statistics of the effects.
- [ ] Validate bokeh with analytic point emitters at multiple depths, focused
  planes, occluding foreground edges, aperture shapes and sample-convergence
  checks. Then compare against actual reference photographs. Existing ghosting
  and incomplete captured geometry must remain separate reconstruction defects.
- [ ] Validate halation against isolated bright edges/emitters and real film
  scans, including dark-region leakage and radius across resolutions. Validate
  grain with density patches, spatial spectra/correlation, deterministic replay,
  resize behavior and reference scans.
- [ ] Record real foreground motion/refinement timing and memory under repeated
  movement, page switching and high-resolution export. Passing color math or
  a synthetic optical fixture is not a sustained-realtime claim.

Implementation order: establish the focus/aperture integration and cancellation
path, then add halation and grain at their intended image-formation stages, then
calibrate target film/lens profiles. The user delegated selection of the first
targets; the chosen set is below. This resolves the target-selection question,
not the calibration or implementation gates.

## Selected first five targets

Use a virtual 35mm/full-frame camera for the initial optical model. The focal
length/aperture combinations below are selectable starting points, not fixed
effects and not claims to reproduce a particular manufacturer's lens. Film and
lens settings must remain independently mixable.

1. **Kodak Portra 400 + 50mm f/1.4 — first/default target.** Natural skin-tone
   reproduction makes this the baseline for people and everyday photography.
   Validate restrained grain and believable focus falloff before stylization.
   [Kodak reference](https://www.kodakprofessional.com/photographers/film/color/kodak-professional-portra-400-film/516).
2. **CineStill 800T + 35mm f/1.4 — second target.** Tungsten/night scenes with
   bright point sources provide the main halation and bokeh-shape reference.
   Use the C-41 800T target, not an interchangeable VISION3/ECN-2 curve: CineStill
   explicitly describes different gamma and processing. [800T reference](https://cinestillfilm.com/products/800tungsten-c41-36exp-35mm-high-speed-color-negative-135),
   [processing distinction](https://help.cinestillfilm.com/hc/en-us/articles/360028873932-Is-CineStill-film-just-repackaged-Motion-Picture-film),
   [halation backing](https://help.cinestillfilm.com/hc/en-us/articles/360028874012-What-is-Remjet).
3. **Kodak Ektar 100 + 28mm f/2.8.** A saturated daylight/travel target with fine
   grain, useful for testing detail preservation and avoiding excessive grain or
   halo defaults. [Kodak reference](https://www.kodak.com/en/still-film/product/professional/ektar-100-film/).
4. **Ilford HP5 Plus 400 + 50mm f/2.** Black-and-white documentary target, with
   monochrome grain and density response validated independently of color-film
   presets. Pin development conditions to the selected published curve; do not
   treat all developers/push processing as the same profile.
   [Ilford technical reference](https://www.ilfordphoto.com/amfile/file/download/file/1903/product/691/).
5. **Kodak Ektachrome E100 + 85mm f/1.8.** A fine-grain color-reversal target
   with neutral color balance; adds a distinct E-6 response and a longer-lens
   bokeh test, rather than another color-negative variation.
   [Kodak reference](https://www.kodak.com/en/still-film/product/professional/ektachrome-e100-film/).

The lens pairings and implementation order are design choices. Manufacturer
descriptions/curves guide targets but do not provide complete spectral, spatial
grain, halation or lens-aberration calibration. Until reference validation
passes, name any exposed approximation as film-inspired, not an accurate stock
or named-lens match. No film stock, lens, dataset or model purchase/download is
authorized or performed by this selection.
