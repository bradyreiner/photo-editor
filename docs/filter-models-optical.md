# CPU optical and spatial effect models

These nine effects are deterministic CPU image-space operators. They accept
linear working RGB and straight alpha, filter in premultiplied form where
spatial sampling is involved, and return straight alpha. Highlight thresholds
use the active working-space luminance coefficients supplied by the render
stage. The luminance row is finite, bounded, and normalized to sum to one; its
coefficients may be signed, as in ACES AP0. Warm highlight tints are normalized
to preserve that working-space Y.
The common effect amount and mask are applied once by the owning effect stack.

The implementations are useful photographic models, not calibrated simulations
of a named lens, film stock, sensor, focal distance, or aperture. Their defaults
are starting points for visual design; they do not make a claim of exact optical
reproduction.

## Models

| Effect | CPU model | Support and limits |
| --- | --- | --- |
| `halation` | Extracts scene-linear highlight energy with a soft knee, blurs that radiance with a normalized separable Gaussian, then adds a working-space-Y-normalized warm emission. The knee uses a quadratic soft shoulder and becomes exactly `max(Y - threshold, 0)` above `threshold + knee`; it does not add a persistent knee offset to bright highlights. The original image remains the base. | Finite support of `ceil(3 * radius * scale)` on each axis. The threshold, knee, and radius are artist controls; the blur does not model film layers, base reflections, or a measured stock response. |
| `anamorphic_streaks` | Extracts highlights, integrates a truncated Gaussian line profile along the configured angle, then applies a narrow Gaussian profile across it. | Finite support includes the line half-length and three cross-width sigmas, with one extra pixel for bilinear sampling. This is a directional point-spread approximation; it does not model a particular anamorphic lens or internal reflections. |
| `starburst` | Extracts highlight radiance and averages a set of narrow, symmetric line-spread kernels. `ray_count` is the number of axes, each contributing two opposite rays. | Finite support is conservatively bounded by the maximum ray length plus three ray-width sigmas. It is a geometric spike model, not a Fourier-optics diffraction PSF for a measured pupil. |
| `lens_bokeh` | Always defocuses the complete premultiplied image through a deterministic numerical integral over a regular polygon aperture. Samples are uniform by aperture area (not uniform by radius); each tap uses premultiplied bilinear sampling. The aperture weights are normalized, so a centered impulse conserves premultiplied energy. `Highlight boost` optionally adds a second bokeh pass from thresholded scene-linear highlights; setting it to zero still leaves full-image defocus active. | Finite support is aperture radius plus one pixel for bilinear sampling. The `integration_samples` control trades kernel smoothness for CPU time. This single aperture model has no depth map, so it cannot model foreground/background ordering, occlusion, or cat-eye clipping; it also does not reproduce lens-specific aberrated bokeh or pupil transmission. |
| `tilt_shift` | Builds an alpha-normalized Gaussian-blurred image, computes signed distance from a configurable focus line, and blends the blur outside a feathered focus band. | Finite Gaussian support is `ceil(3 * blur_radius * scale)`. This is a planar selective-focus effect; it is not a thin-lens/Scheimpflug camera solution and cannot infer scene depth. |
| `zoom_blur` | Numerically averages bilinear samples along radial source paths from a configurable frame-relative center. | Requires whole-frame admission. The operator reports full-frame support when frame dimensions are supplied and rejects partial-frame input; it must not be run as an independently padded tile. |
| `spin_blur` | Numerically averages bilinear samples along angular source paths around a configurable frame-relative center. | Requires whole-frame admission. The operator reports full-frame support when frame dimensions are supplied and rejects partial-frame input; it must not be run as an independently padded tile. |
| `surface_blur` | Direct bilateral filtering with a Gaussian spatial kernel and a Gaussian Euclidean range kernel over linear RGB. Alpha participates as sample coverage; fully transparent taps do not tint covered samples, and a fully transparent center is copied unchanged because its straight RGB is undefined. | Finite, circular document-space support. Effective radius is limited to 24 raster pixels per axis to keep the direct `O(r²)` CPU kernel bounded. This is a bilateral surface smoother, not a segmentation-aware skin retoucher. |
| `high_pass` | Subtracts an alpha-normalized Gaussian low-frequency image from the source and offsets the residual around linear RGB 0.5. A constant field therefore produces the numeric value 0.5 while alpha is preserved. This is a gray-offset extraction raster: because the pipeline is scene-linear, 0.5 is not a neutral Overlay value under a display transfer function, and the operator does not itself define a sharpening blend/composite. | Finite support is `ceil(3 * radius * scale)`. Negative and above-one RGB values are retained in the linear working image. |

All distances are in document pixels and are converted with the render options'
X/Y scales. Tile callers must use the support returned by
`optical_photo_effect_support`; it includes bilinear sampling margins where
needed. The effect API is transactional: cancellation, invalid input, and
scratch exhaustion leave the caller's raster unchanged. `max_scratch_bytes`
accounts for temporary full-size pixel arrays and aperture integration points,
excluding the input raster. The common stack must check
`optical_photo_effect_requires_full_frame` before tile planning for zoom and
spin blur. A whole-frame render may still return `ResourceLimit` when the
configured scratch budget cannot fit its working arrays.

## Numeric and invariant coverage

`tests/photo_optical_effects_test.cpp` checks strict parameter round trips and
transactional rejection, constant-field invariance for surface blur, the gray
high-pass response of a constant field, a normalized Gaussian halation impulse,
polygon-aperture anisotropy, directional starburst profiles, whole-frame
admission, finite output, cancellation, and scratch-budget failure.
`tests/photo_effect_reference_accuracy_test.cpp` adds a direct 2-D Gaussian
reference for high pass, an above-knee emission reference, premultiplied energy
integrals for the flare kernels and polygon aperture, surface transparency
invariance, focus-band coordinates, and radial/angular sampling references.
These are deterministic algorithm checks. They do not establish empirical
agreement with a named camera, film stock, or lens.

To include the test in the native test graph:

```cmake
add_executable(photo_optical_effects_test photo_optical_effects_test.cpp)
target_link_libraries(photo_optical_effects_test PRIVATE photo_engine)
photo_editor_apply_warnings(photo_optical_effects_test)
add_test(NAME photo_optical_effects_test COMMAND photo_optical_effects_test)
```

The independent reference checks can be registered with:

```cmake
add_executable(photo_effect_reference_accuracy_test photo_effect_reference_accuracy_test.cpp)
target_link_libraries(photo_effect_reference_accuracy_test PRIVATE photo_engine)
photo_editor_apply_warnings(photo_effect_reference_accuracy_test)
add_test(NAME photo_effect_reference_accuracy_test COMMAND photo_effect_reference_accuracy_test)
```

## References

- Kodak's glossary defines anti-halation backing as a layer that absorbs light
  that would otherwise reflect back into the emulsion: [Glossary of Motion
  Picture Terms](https://www.kodak.com/en/motion/page/glossary-of-motion-picture-terms/).
  The `halation` implementation uses only the broad highlight-scatter concept;
  its Gaussian/tint controls are an artistic approximation.
- Starburst diffraction comes from propagation past the limiting aperture.
  Fourier optics relates the diffraction-limited point-spread function to the
  exit-pupil shape: [Computational Imaging Prediction of Starburst-Effect
  Diffraction Spikes](https://pmc.ncbi.nlm.nih.gov/articles/PMC6240111/).
- Polygon aperture shape affects bokeh, and polygon-specific gathering kernels
  are a known post-process approximation: [McIntosh, *Efficiently Simulating
  the Bokeh of Polygonal Apertures in a Post-Process Depth of Field
  Shader*](https://doi.org/10.1111/j.1467-8659.2012.02097.x).
- The edge-preserving smoothing model follows the spatial and photometric
  weighting principle of the bilateral filter: [Tomasi and Manduchi,
  *Bilateral Filtering for Gray and Color Images*](https://doi.org/10.1109/ICCV.1998.710815).
- A real tilted lens changes the plane of focus under the Scheimpflug relation;
  a 2D focus band cannot recover that scene geometry: [*A Comprehensive and
  Versatile Camera Model for Cameras with Tilt Lenses*](https://link.springer.com/article/10.1007/s11263-016-0964-8).
