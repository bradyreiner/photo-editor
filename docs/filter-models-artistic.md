# Artistic effect operator models

These CPU operators consume straight-alpha, scene-linear working RGB. Luminance
uses the XYZ Y coefficients of the current working gamut supplied by the render
owner, including signed coefficients where a wide-gamut matrix requires them.
They are non-destructive color or spatial transforms; stack amount and mask
interpolation remain the caller's responsibility. Algorithm version 1 uses the definitions
below. `PhotoEffectRenderOptions::scale_x/scale_y` convert document-space sizes
to raster pixels, and `origin_x/origin_y` anchor spatial patterns to the document
so padded tiles keep the same phase.

| Operator | Version 1 model | Fidelity boundary |
| --- | --- | --- |
| `gradient_map` | Compute scene-linear working-gamut luminance, clamp it to `[0,1]`, then piecewise-linearly interpolate the ordered custom stops. Stop RGB values are scene-linear. | A tonal remap, not a print or pigment model. The tone-to-gradient behavior matches the general definition of gradient mapping in [Adobe's adjustment guide](https://helpx.adobe.com/photoshop/desktop/adjust-color/color-effects-techniques/apply-special-color-effects-to-images.html). |
| `solarization` | Reflect working-gamut luminance around the selected threshold above the threshold (`2T - Y`); optional softness blends from the identity curve to that reflection. Add the luminance delta equally to RGB, preserving channel differences. | A digital Sabattier-style tone curve. It does not simulate film exposure, emulsion chemistry, or development. The historical terms solarization and Sabattier describe different physical processes; the operator is intentionally a tone-curve effect. |
| `color_halftone` | Independently threshold RGB channels against periodic, rotated circular-dot screens. Dot radius follows the square root of inverse channel tone; each channel has its own angle. | A three-screen RGB graphic approximation, not a calibrated CMYK press separation, ink-overprint, or printer RIP. The multi-screen concept is consistent with [Adobe's Color Halftone description](https://helpx.adobe.com/photoshop/desktop/effects-filters/artistic-stylize-filters/apply-distortion-and-stylize-filters.html). |
| `dither` | Clamp each scene-linear working RGB channel to `[0,1]` and quantize to evenly spaced linear-light levels. Ordered mode uses a recursive Bayer threshold matrix. Floyd–Steinberg mode diffuses linear-light quantization error with the 7/16, 3/16, 5/16, 1/16 four-neighbor stencil. | Standard digital quantizers applied in the working-linear domain. This is not sRGB-code-value dithering, an output-profile conversion, or calibrated print/RIP simulation. Floyd–Steinberg is causal from the upper-left pixel and requires the complete raster, starting at `(0,0)`; it cannot be rendered independently in tiles. The ordered and error-diffusion families are covered in Ulichney's [Digital Halftoning](https://doi.org/10.7551/mitpress/2421.001.0001); the original Floyd–Steinberg paper is *An Adaptive Algorithm for Spatial Greyscale*, SID 17(2), 75–77 (1976). |
| `crosshatch` | Lay down up to four document-anchored line screens at configured angles. Each layer activates below its own luminance threshold; ink strength accumulates multiplicatively. | A deterministic graphic line-screen treatment, not pen-pressure, paper, or hand-drawn stroke simulation. |
| `ink_outline` | Compute Sobel luminance gradients and darken pixels according to gradient magnitude, threshold, and strength. | A local edge-emphasis filter. It does not infer semantic contours or model ink spread. |
| `watercolor` | Apply a small spatial-and-luminance bilateral neighborhood average, darken local transitions, and add seeded low-amplitude pigment-like variation in document coordinates. Alpha is preserved. | An artistic watercolorization approximation. It does not simulate water flow, pigment transport, layered translucent glazes, paper fibers, or Kubelka–Munk optical mixing. Curtis et al.'s [Computer-Generated Watercolor](https://grail.cs.washington.edu/wp-content/uploads/2015/08/curtis-1997-cgw.pdf) uses a shallow-water fluid simulation and glaze compositing; this operator does neither. |
| `oil_paint` | In a local square neighborhood, quantize luminance into bins, select the most populated bin, and return its alpha-weighted mean RGB. | A neighborhood-histogram stylizer, not bristle geometry, paint deposition, pigment mixing, or a stroke-based painterly renderer. The [OpenCV oilPainting API](https://docs.opencv.org/5.0/extra_modules/xphoto.html) likewise exposes neighborhood size and histogram intensity division; results need not match OpenCV. |
| `pixel_mosaic` | Assign each pixel to a square grid cell, regular hexagonal Voronoi cell, or deterministic jittered-center Voronoi cell. Each cell samples the nearest source pixel to its center and holds that color and alpha over the cell. | These are three distinct, implemented tessellations. Cells are representative point samples rather than area averages: this keeps work bounded independently of cell coverage, but does not preserve each cell's mean color. Irregular mode is a jittered Voronoi partition, not unconstrained region segmentation. Padded input must cover the reported support around each sample center. |
| `rgb_glitch` | Hash each document-anchored horizontal band from the saved seed. Within active bands, sample red and blue at opposing horizontal offsets and all channels at the configured vertical offset; green remains registered. | A deterministic channel-registration graphic effect, not a model of a particular analog video standard, codec, or transmission fault. |
| `displacement_map` | Sample an immutable RGBA data raster bilinearly at normalized full-frame pixel centers; map dimensions may differ from the document. Red and green encode signed x/y offsets around `0.5`; each texel's alpha blends its displacement to neutral before map interpolation. Source sampling is bilinear and alpha-normalized. | A direct image-space map warp, matching the channel-driven model in [SVG `feDisplacementMap`](https://www.w3.org/TR/SVG11/filters#feDisplacementMap). Map channels are data: they are not sRGB-decoded or color-converted. The resolver maps red/green byte 128 exactly to `0.5`; a map with zero alpha or red/green equal to `0.5` produces no shift. |

## Render support and failure behavior

`artistic_photo_effect_support()` reports the raster padding needed around a
requested output tile. Local neighborhood, mosaic, glitch, and displacement
operators are tile-stable when callers provide that padding and pass absolute
document origin plus full-frame dimensions. Ordered dithering and all
coordinate-generated patterns are independently tile-stable. Floyd–Steinberg
error diffusion is causal across scanlines and is explicitly whole-frame only;
`artistic_photo_effect_requires_full_frame()` exposes that admission check.

Neighborhood filters have bounded but different preview costs. At 1:1 scale,
watercolor and oil paint each inspect 25 samples per output pixel at default
radius 2; the maximum radius 8 inspects 289. Keep radius 2 for responsive
previews and reserve larger neighborhoods for deliberate full-resolution work.
Ink outline uses a fixed 3-by-3 Sobel stencil. Bayer, crosshatch, halftone, and
mosaic have bounded per-pixel work. Floyd–Steinberg uses two error rows in
addition to the transactional output buffer. The output buffer costs one
`ColorPixel` per input pixel. Displacement keeps the resolved map in an immutable
shared binding and uses one bilinear source sample per output pixel.

All effects leave alpha unchanged unless the spatial sampling operator changes
which source pixel is selected (`pixel_mosaic`, `displacement_map`). Interpolated
spatial samples use premultiplied-alpha accumulation and convert back to straight
alpha. Operations are transactional on invalid input, cancellation, output
overflow, or scratch-budget failure. The displacement map binding is an
immutable shared raster resolved by the owner from its persisted asset reference;
`asset_ref` is the lowercase 64-hex SHA-256 of the canonical PNG, whose hash and
decoded dimensions the resolver verifies. The map can use arbitrary dimensions;
the renderer bilinearly samples at normalized full-frame pixel centers and
clamps map-edge lookups. RGBA channels are data values without transfer decoding
or gamut conversion; the resolver makes encoded red/green byte 128 exactly
neutral at `0.5`. The
effect payload serializes the asset identity and dimensions, never the transient
raster bytes.
