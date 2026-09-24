# RAW Reference Fixture Plan

> Supporting fixture specification. Synthetic fixtures described here cannot satisfy the licensed
> real-RAW, independent-reference, hardware, or editorial gates in
> `docs/color-engine-and-app-foundations-completion-strategy.md`.

The current CI gate uses synthetic RAW-style fixtures in `color_chart_validation_test`.
Those fixtures cover neutral, skin, sky, and saturated patches with deterministic
camera metadata, input-stage white balance, camera profile resolution, RAW level
normalization, highlight reconstruction, and final output gamut compression.

## Synthetic CI Fixtures

- Keep synthetic RAW fixtures small and generated in code so CI does not depend on
  licensed image assets.
- Each fixture must declare the target patch family it protects: neutral, skin,
  sky, or saturated color.
- Synthetic RAW tests must assert qualitative color invariants, not only "no
  crash": neutral chroma stays low, skin and sky hue stay near reference chart
  patches, saturated colors report output-gamut pressure, and output pixels stay
  finite and bounded after the selected transform.
- When adding new camera/input fields, extend the synthetic fixture state first
  so the field is covered before introducing external corpus files.

## Licensed Real RAW Fixtures

Real RAW fixtures should be added only when the repository has clear rights to
redistribute them in CI.

1. Store license notes beside the fixture manifest, including source, license,
   permitted use, and whether redistribution is allowed.
2. Add checksums and camera metadata to the manifest so fixture drift is
   detectable.
3. Keep real RAW assets out of the source tree until the license permits checked
   in redistribution. If redistribution is not allowed, wire them as optional
   local/perf artifacts, not required CI inputs.
4. For each real RAW, include a reference target for at least one neutral patch,
   one skin patch, one sky patch, and one saturated patch, plus acceptable
   Delta-E or hue/chroma thresholds.
5. CI may graduate a real RAW fixture from optional to required only after the
   license, manifest, decode path, and reference thresholds are reviewed
   together.
