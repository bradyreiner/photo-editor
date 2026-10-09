# CPU filter reference accuracy and scope

This audit covers the 15 original registered CPU effects and the nine added
optical effects. It checks equations, kernel normalization, coordinate use,
alpha behavior, and bounded support. The checks are numerical references for
the implemented models; they are not a claim of physical equivalence to a
camera, lens, film stock, paper process, or human visual approval.

## Operator inventory

| Operator | Current CPU model and reference | Accuracy boundary |
| --- | --- | --- |
| `motion_blur` | Uniform, bilinearly sampled line exposure. Existing impulse test checks the five-tap horizontal profile, rotation, and transparent-edge premultiplication. | A constant-velocity image-space path, not a shutter/camera motion reconstruction. Sample count is bounded from path length. |
| `dragged_shutter` | Directional exposure with center-weighted falloff, highlight weighting, and optional mask-based subject protection. Existing tests cover protection, taper direction, and brighter-highlight retention. | The spatial falloff is a designed exposure field; it has no temporal scene motion or occlusion model. |
| `unsharp_mask` | Source plus thresholded residual from an alpha-normalized Gaussian. Existing impulse tests check positive and negative detail excursions and threshold suppression. | A linear-working-RGB detail operator; no perceptual-space or display-referred sharpening curve is implied. |
| `posterize_linear_rgb` | Per-channel nearest-level quantization on a grid that extends to negative/HDR values. Existing test checks exact levels and partial amount. | Quantizes scene-linear channels; it is not a display-referred posterize look. |
| `box_blur` | Separable uniform square kernel. Existing impulse reference checks its square support. | Its square footprint is intentional and differs from a circular aperture or Gaussian PSF. |
| `gaussian_blur` | Separable normalized Gaussian, truncated at `ceil(3 sigma)`. Existing test compares every pixel against direct 2-D convolution over fractional alpha/HDR input and exercises edge modes. | The finite three-sigma truncation is an implementation boundary; it is not an infinite Gaussian. |
| `optical_diffusion` | Extracts highlight excess, blurs it, and adds a warm scatter term. Existing tests cover highlight selection, threshold behavior, alpha, warmth, and tiled agreement. | An artistic glow model. Its threshold, warmth, spread, and shape are not measured from a stock or optical bench. |
| `imperfect_print` | Seeded paper variation, registered RGB plates, ink spread, and channelwise density. The new reference test checks the closed-form density subcase with all texture and spread disabled. | `1-c` is an illustrative linear-RGB ink coordinate, not optical density, spectral reflectance, CMYK separation, or a profiled print transform. |
| `film_grain` | Seeded document-space value noise scaled by tone and color controls. Existing tests check deterministic output and seed sensitivity. | Procedural noise, not a measured grain-size distribution, film-stock response, or scanned-grain synthesis. |
| `light_leak` | Frame-relative Gaussian wash with seeded texture and a warm-to-cool channel tint. Existing tests check render, alpha, determinism, and serialization. | A synthetic additive veil, not a traced internal reflection or a wavelength-dependent flare model. |
| `split_tone` | Tone-weighted hue tint, normalized to the configured working-space Y row. The new reference test checks Y preservation with signed ACES AP0 coefficients. | A color grade in the active linear RGB basis, not a film response curve. |
| `vignette` | Frame-relative Lp radial field, smooth feather, and exposure multiplier. The new reference test checks center and corner exposure against the equation. | A parametric falloff, not lens shading calibrated against aperture, focal length, sensor, or lens field measurements. |
| `duotone` | Tone interpolation between working-Y-normalized hue endpoints. Existing tests cover output/codec behavior; the same signed-Y normalization helper is exercised by the split-tone reference. | A two-hue grade, not a two-ink print simulation. |
| `soft_fade` | Explicit linear-light lift, contrast pivot, and saturation interpolation. The new reference test compares a pixel against that equation. | Artist-defined curve; “fade” does not promise a paper or emulsion model. |
| `invert_linear_rgb` | Applies `1-c` independently to linear RGB, preserving alpha. The new reference test includes negative and above-one channels. | Linear-channel inversion is not equivalent to inverting a display-encoded image. |
| `halation` | Extracts scene-linear highlight energy with a quadratic soft knee, applies a normalized Gaussian, and adds a Y-normalized warm emission. The new test checks the exact above-knee radiance and the existing test checks the Gaussian impulse. | This is only the broad highlight-scatter idea associated with halation. It does not model light returning through a film base, layered emulsion absorption, or a measured stock. |
| `anamorphic_streaks` | Extracts highlights and applies normalized directional Gaussian line and cross kernels. The new test integrates the resulting impulse energy. | A directional image-space kernel, not a particular anamorphic lens, coating, reflection path, or measured point-spread function. |
| `starburst` | A normalized average of symmetric narrow line-spread kernels. The new test integrates impulse energy and the existing test checks directionality. | Real aperture diffraction is a Fourier-optics PSF; these line spikes are a graphic approximation and are not pupil-derived diffraction. |
| `lens_bokeh` | Full-image defocus by numerical area integration over a regular polygon aperture, followed by optional highlight-only bokeh boost. The new tests check normalized impulse-energy conservation at zero boost and boosted energy; the existing test checks polygon anisotropy and dim-detail defocus below the highlight threshold. | Uniform area is an ideal geometric kernel for a uniform-transmission defocus pupil. Without depth, the model cannot represent foreground/background ordering, occlusion, cat-eye clipping, aberration, or lens-specific bokeh brightness. |
| `tilt_shift` | A Gaussian-blurred image is mixed outside a frame-relative planar focus band. The new test checks exact in-band source preservation. | A two-dimensional focus mask; no scene depth, lens tilt, Scheimpflug geometry, or thin-lens focus calculation is available. |
| `zoom_blur` | A finite midpoint quadrature integrates bilinear samples along inward radial source paths. The new test checks the four-sample linear-ramp result using the minimum admitted sample count. | A synthetic exposure path. Because it can sample anywhere in the frame, it requires whole-frame admission; a padded tile is not equivalent. |
| `spin_blur` | A finite midpoint quadrature integrates inverse-rotated source samples around a frame-relative center. The new test checks a four-sample arc on a linear ramp using the minimum admitted sample count. | A synthetic exposure arc. It also requires whole-frame admission for exact output. |
| `surface_blur` | Direct bilateral filtering with Gaussian spatial and Euclidean linear-RGB range weights. The new test checks an edge step and invariance to undefined RGB under zero alpha. | It is not segmentation-aware retouching. The bounded direct kernel costs `O(N r²)`; the maximum raster radius is 24 pixels, and this accuracy test is not a full-resolution performance benchmark. |
| `high_pass` | Produces `0.5 + strength * (source - alpha_normalized_Gaussian(source))`, preserving alpha. The new test compares it with an independent direct 2-D Gaussian calculation. | The gray offset is an extraction convention. In scene-linear RGB, 0.5 is not an Overlay-neutral value; this operator alone is not a complete sharpening composite. |

The older 15 operators already have focused contract or numeric coverage in
`photo_effect_gaussian_test.cpp`, `photo_effect_spatial_test.cpp`, and
`photo_effect_posterize_test.cpp`. The new `photo_effect_reference_accuracy_test`
adds independent equations and references where the existing tests were
invariant-, workflow-, or codec-oriented. `photo_optical_effects_test.cpp`
continues to cover the nine new operators' parameter contract, cancellation,
scratch budget, finite outputs, and selected shape invariants.

## Kernel, color, alpha, and execution notes

All operators consume linear working RGB with straight alpha. Spatial filters
use premultiplied samples and unassociate where needed. Highlight extraction and
hue normalization use the compositor-provided XYZ-Y coefficients of the active
working gamut. Those coefficients may be signed; ACES AP0's blue coefficient is
negative. The AP0 test ensures a hue tint preserves that supplied Y row rather
than hard-coding Rec.709.

The halation soft knee follows the standard soft-threshold construction: with
threshold `T` and knee `K`, let `q = clamp(Y - (T-K), 0, 2K)` and let
`E = max(max(Y-T, 0), q²/(4K))`. For `K=0`, `E=max(Y-T,0)`. Thus the soft
transition begins at `T-K`, and once `Y >= T+K`, emitted energy is exactly
`Y-T`. This avoids the earlier permanent `+K` brightness error in fully selected
highlights. The extracted coverage has a separate smooth gate and does not turn
the model into a calibrated energy response.

The surface bilateral range reference is undefined at a fully transparent
center because its straight RGB carries no visible color. Such a center is now
copied without letting hidden RGB affect nearby or newly visible samples.
Transparent taps are ignored as color contributors. The included regression
compares two rasters with identical premultiplied content but different hidden
RGB.

Finite-support operators must be padded by the declared support. Absolute frame
origin/dimensions keep frame-based falloffs stable between tiles. `zoom_blur`
and `spin_blur` sample globally and must be routed through
`photo_effect_stack_requires_full_frame`; the renderer declares full-frame
support and rejects partial-frame execution. All nine optical operators have
bounded scratch estimates and cancellation checks. Bounded allocation is not a
latency guarantee: in particular, `surface_blur`'s direct neighborhood scan can
be expensive at full-resolution images. No 4K throughput or interactive
performance claim is made here.

## Reference basis

These sources motivate the model boundaries. They do not validate this specific
implementation against physical equipment:

- The [Kodak Motion Picture Terms glossary](https://www.kodak.com/en/motion/page/glossary-of-motion-picture-terms/)
  describes anti-halation backing as absorbing light that would otherwise
  reflect back into the emulsion. The CPU effect models highlight scatter only.
- [R. H. F. van Houten et al., *Computational Imaging Prediction of
  Starburst-Effect Diffraction Spikes*](https://pmc.ncbi.nlm.nih.gov/articles/PMC6240111/)
  describes the Fourier-optics relation between exit-pupil shape and the
  diffraction-limited point-spread function. The line-ray effect here does not
  compute that pupil transform.
- [L. McIntosh et al., *Efficiently Simulating the Bokeh of Polygonal Apertures
  in a Post-Process Depth of Field Shader*](https://onlinelibrary.wiley.com/doi/10.1111/j.1467-8659.2012.02097.x)
  discusses polygonal aperture shape in post-process depth-of-field. This
  implementation integrates a regular aperture shape but has no depth buffer.
- [C. Tomasi and R. Manduchi, *Bilateral Filtering for Gray and Color Images*](https://users.cs.duke.edu/~tomasi/papers/tomasi/tomasiIccv98.pdf)
  is the primary reference for spatial and photometric weighting in bilateral
  smoothing. This version uses Euclidean distance in active linear RGB rather
  than the paper's perceptual color metric.
- [C. Steger, *A Comprehensive and Versatile Camera Model for Cameras with Tilt
  Lenses*](https://link.springer.com/article/10.1007/s11263-016-0964-8)
  models tilted-lens imaging geometry. The 2-D focus-band effect is a graphic
  approximation and does not implement that camera model.

## Native test registration

Add this target to `tests/CMakeLists.txt` when integrating the focused
reference suite:

```cmake
add_executable(photo_effect_reference_accuracy_test photo_effect_reference_accuracy_test.cpp)
target_link_libraries(photo_effect_reference_accuracy_test PRIVATE photo_engine)
photo_editor_apply_warnings(photo_effect_reference_accuracy_test)
add_test(NAME photo_effect_reference_accuracy_test COMMAND photo_effect_reference_accuracy_test)
```
