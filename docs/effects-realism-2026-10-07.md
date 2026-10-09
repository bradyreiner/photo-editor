# Filter and text material expansion

The photo effect catalog now contains 35 individual operators, including these
20 additions. They use the existing editable layer stack, amount, selection
coverage, history, recipe persistence, and export paths.

| Group | New effects |
| --- | --- |
| Optical | Film halation, anamorphic streaks, starburst |
| Blur | Lens bokeh, tilt-shift, zoom blur, spin blur, surface blur |
| Sharpen | High-pass detail |
| Color | Gradient map, solarization |
| Print | Color halftone, ordered Bayer / Floyd–Steinberg dither |
| Artistic | Crosshatch, ink outline, watercolor, oil paint, square / hexagonal / irregular pixel mosaic |
| Distort | RGB glitch, displacement map |

The picker groups and sorts effects, supports search, and previews directly on
the undimmed canvas. Its settings body scrolls while Cancel and Apply stay
available. Extended controls use labeled choices for discrete algorithms,
percentages for frame-relative coordinates, and document pixels for distances.
Gradient maps support 2–32 editable color-managed stops. Imported displacement
maps are data rasters stored by verified content hash in the project recipe;
they continue to work after the original external image is removed.

Cancellation restores the original layer before the popup's exit animation.
Applying a preview creates one history transaction. Whole-frame-only operators
are admitted as complete rasters rather than independently padded tiles, so
zoom blur, spin blur, and causal error diffusion do not acquire tile seams.
Other operators use conservative support and document-anchored coordinates.

Text material rendering now uses GGX/Smith/Schlick metal shading, published
conductor color references decoded into linear RGB, neutral studio softboxes,
and working-gamut conversions. Glass and plastic use dielectric Fresnel
reflection and transmission with lower-stack sampling. Glyphs remain editable,
and their alpha coverage is preserved. The 13 existing material styles are
exercised through the typography inspector, history, save/reopen, and export.

## Accuracy targets

- [Optical models](filter-models-optical.md) define normalized aperture and
  scattering kernels, highlight extraction, sampling paths, bilateral smoothing,
  and detail extraction.
- [Independent optical references](filter-reference-accuracy.md) distinguish
  numeric agreement with those definitions from agreement with real equipment.
- [Artistic models](filter-models-artistic.md) define the actual quantizers,
  screens, local stylizers, tessellations, and data-map warp.
- [Text materials](text-material-emulation-model.md) record the conductor
  references, lighting model, working-space behavior, and approximation limits.

These implementations do not claim perfect reproduction of a named lens,
film stock, paint process, print press, or real material under arbitrary light.
Optical kernels have no scene depth or measured lens response; watercolor and
oil paint are stylizers; RGB halftone is not a CMYK press model. Metal text has
an inferred bevel and authored studio illumination rather than scene geometry,
spectral measurements, or reflections reconstructed from the photograph.

## Verification and review artifacts

The focused CTest run passed 14/14 tests: both new effect modules, independent
references, the complete extended stack, Gaussian and spatial regression tests,
codec, layer alpha, layer effect composition, content padding, typography styles,
the grouped browser, the original native-controller workflow, and the extended
20-filter / 13-material workflow. A separate native Cocoa run verified every
extended case on the licensed night photograph, including gradient-stop edits,
the dither algorithm picker, immediate cancellation, one-step undo/redo, and
exact reopened exports. QML lint also passed for both changed filter components.

The bundled app was linked from the configured CMake objects and flags after
compiling a concurrent Library image-provider addition and refreshing generated
QML resources. Its isolated native startup reported `packagedQmlLoaded=true`
and reached `desktop.startup.ready`. The broader managed-runtime proof failed
with `Local runtime startup timed out. Retry setup.`; it also reported a binding
loop in the concurrent Library delivery panel. This is not a passing full-app
or local-AI runtime qualification. The effect workflow evidence above remains
a separate, passing check of the actual filter and text controllers and views.

The extended stack test covers all 20 operators with signed AP0 luminance
weights, exact parameter serialization, fractional amount/selection coverage,
whole/tiled/private/streamed parity, zero-coverage identity, bounded scratch
admission, and transactional cancellation. Its optional
`PHOTO_EFFECT_4K_REFERENCE=1` run verified exact whole-frame/stream hashes for
3840×2160 zoom blur, spin blur, and Floyd–Steinberg dithering under the default
512 MiB scratch budget. The blur checks use four integration samples; this is
a correctness and memory-admission check, not a default-quality latency claim.

Native Cocoa captures, flattened exports, editable recipes, logs, source
attribution, and hashes are recorded in
[the evidence manifest](evidence/effects-realism-2026-10-07/manifest.json).
The photographic workflow uses a 640-pixel-wide licensed source. Native
interaction and rendered output are review evidence; they do not constitute
human photographic or material-reference qualification across a broader corpus.

Reproduce focused checks with the `dev-macos-debug` preset and an isolated
build directory:

```sh
cmake --preset dev-macos-debug -B build/effect-realism -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/effect-realism --target photo_editor_app photo_effect_workflow_test photo_optical_effects_test photo_artistic_effects_test photo_effect_reference_accuracy_test photo_extended_effect_stack_test
ctest --test-dir build/effect-realism -R '^(photo_optical_effects_test|photo_artistic_effects_test|photo_effect_reference_accuracy_test|photo_extended_effect_stack_test|photo_extended_effects_workflow_test)$' --output-on-failure
PHOTO_EFFECT_4K_REFERENCE=1 build/effect-realism/tests/photo_extended_effect_stack_test
```
