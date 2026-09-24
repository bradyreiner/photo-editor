# Layer workflow performance — September 10, 2026

This pass removes repeated work from selection-scoped layers during photo
opening, color edits, full-resolution restoration and flattened export. It
preserves the scalar color operations and full-resolution output.

## Changes

- Geometry and paint selections reuse exact alpha rasters in a process-local
  LRU bounded to four entries and 64 MiB of retained keys and alpha payloads.
  Keys include the complete compiled mask set and output dimensions. Oversized
  entries render normally without caching. Color/luminance/depth selections,
  unknown kinds, image-dependent refinement and edge-aware feather/blur bypass
  the cache. Eligible misses also avoid copying pixels into an unused edge
  context. General brush/mask evaluation retains its original scheduling.
- Neutral normal adjustment layers avoid per-pixel recipe application and
  interpolation. Alpha buffers are allocated only when a following participating
  layer clips to them. Disabled layers, groups and invalid pixel layers preserve
  the existing clipping behavior. Nonfinite RGB input falls back to scalar handling.
- Active layer curves build their parametric, master, luminance and channel
  evaluators once per layer instead of rebuilding their vectors and tangents
  for each pixel. The evaluation order and color math are unchanged. Curve
  layers without common adjustments process independent rows through the
  existing worker scheduler; caller-provided callbacks stay on their original
  thread. Each worker writes disjoint pixels and optional alpha entries.

Layer shortcuts require an explicit standard-adjustment callback opt-in.
Arbitrary callbacks retain the scalar path. Cache diagnostics are available via
`PHOTO_EDITOR_PROFILE_LAYER_MASK_CACHE=1`; they log dimensions and retained
bytes without recipe payloads or source identifiers.

## Focused proof

Release measurements alternate scalar and optimized execution in one process;
timing assertions are separate from correctness tests.

| Operation | Scalar median | Optimized median |
| --- | ---: | ---: |
| Two neutral layers, 2048 × 1365 | 60.098 ms | 4.690 ms |
| Active parametric and five point curves, 512 × 341 | 159.500 ms | 5.259 ms |
| Repeated unchanged mask, 1536 × 1024 | 163.038 ms | 0.0204 ms |

The neutral-layer test requires bit-identical RGBA for 64 composition cases,
16 curve variants, odd-sized parallel rows with clipped alpha, transformed
pixel layers and signed/HDR/alpha edge cases.
The mask test compares 22 mask variants at three sizes with the uncached scalar
oracle, including changed source pixels, key invalidation, eviction and
concurrent workers.

## Workflow evidence and limits

The private fixture is a 24-megapixel CR2 with a large
recovered recipe containing two neutral selection-scoped adjustment layers.
The benchmark exercises draft pixels, matched-resolution reference pixels,
full preview pixels and an actual flattened export. Native textures are enabled.

Three runs per executable alternated order (before/after, after/before,
before/after), without concurrent builds or tests:

| Measurement | Before median | After median | Change |
| --- | ---: | ---: | ---: |
| Full curve-edit render | 2,983 ms | 1,957 ms | −34.39% |
| Full photo-selection render | 4,307 ms | 4,025 ms | −6.55% |
| First curve-feedback render | 135 ms | 129 ms | −4.44% |
| Actual flattened export | 18,443 ms | 17,228 ms | −6.59% |
| Peak RSS | 5,382,766,592 B | 5,216,894,976 B | −3.08% |

The final comparison passes the 20% full-edit improvement target, 10% latency
regression guards and 10% maximum-RSS guard. All three PNG outputs are
byte-identical across all six runs. Draft/reference maximum channel error stays
0/255, and native full/export error stays 1/255. Full output was visually
inspected. `final-comparison.json` contains the complete rows and evaluated gates.
These are local observations on one recipe, not corpus latency percentiles.
First feedback remains above the desired 100 ms budget.

A separate stress fixture activates parametric and all five point-curve channels
on an unmasked adjustment layer, so the curves visibly affect the full RAW.
An initial selection-scoped fixture was rejected because its empty selection
made the curves invisible. The corrected scalar, prepared-curve and final
parallel-curve outputs are byte-identical to each other, and all differ from the
neutral-recipe output. Draft/reference error remains 0/255 and full/export
error remains 1/255.

| Full-image active curves, one run each | Original scalar | Prepared curves | Prepared + parallel rows |
| --- | ---: | ---: | ---: |
| Full edit render | 32,838 ms | 9,809 ms | 4,076 ms |
| Actual flattened export | 52,710 ms | 24,534 ms | 19,502 ms |

The final full-edit reduction is 87.59%; export reduces 63.00%. This stress
comparison is diagnostic rather than a repeated percentile gate. The final
binary also reruns the original recipe; the row-parallel branch is inactive for
its neutral layers. `parallel-curves-comparison.json` records the exact PNG
checks against the archived scalar renderer.

Earlier attempts had substantial timing noise and failed some first-feedback
and selection guardrails. Those runs are retained as evidence; their acceptance
is not claimed for the final code. The user explicitly requested continued
optimization instead of stopping at the default three-attempt boundary. The
subsequent profile identified the neutral-layer and per-pixel curve preparation
costs measured above.

Actual QML routing also exercised Library, Import, Edit, Masks, brush feedback,
Carousel, Timeline and Export and captured screenshots. Cold Carousel/Timeline
visits still missed budgets. That driver loads QML from source files, so cold
component timings include compilation and do not establish packaged-app startup
performance. Import/Export navigation does not establish bulk throughput.
Zoom input-to-frame and sustained library scrolling remain separate measurements.

The interaction gate now checks the driver's actual
`global_exposure_to_draft_scheduled` summary instead of the nonexistent
`tool_adjust_to_draft_scheduled`, with unchanged 75/125 ms thresholds. The driver
also logs actual `flattened_export_to_file` duration. The original slow first
brush commit remains a recorded budget failure; fixing the metric name does
not remove it.

## Verification

The Release desktop app and affected test binaries build without warnings.
Six focused native tests pass in 6.41 seconds: `color_pipeline_test`,
`mask_raster_cache_test`, `neutral_layer_elision_test`,
`compiled_local_adjustment_test`, `render_parity_test` and
`layer_lut_render_parity_test`. The interaction gate passes Python syntax and
scoped whitespace checks. Native pixel proof covers the original recipe and the active full-image curve
recipe, with byte-identical before/after PNGs.

The final binary's original-recipe confirmation renders the full edit in
2,022 ms and exports in 17,860 ms, with the same three baseline PNGs. Framehouse
was relaunched through `run-native.sh --no-build` and confirmed running from
the Release bundle. The recipe outbox was backed up first and contained zero
pending/active saves. The live Library subsequently reported a 927 ms maximum
frame during initial loading; this pass does not claim cold Library jank is
resolved. Launch and final confirmation logs remain in the private artifact root.

## Reproduction

```sh
cmake --build build/release-macos --target neutral_layer_elision_test mask_raster_cache_test interaction_workflow_benchmark_driver
build/release-macos/tests/neutral_layer_elision_test
build/release-macos/tests/neutral_layer_elision_test --benchmark
build/release-macos/tests/mask_raster_cache_test
build/release-macos/tests/mask_raster_cache_test --benchmark
```

Private artifacts are in `/private/tmp/framehouse-workflow-next/`:
`layer-parallel-benchmark.log` (final), `layer-optimized-benchmark.log`
(before row parallelism), `mask-baseline.log`, `mask-candidate.log`,
`profile-current.sample.txt`, `neutral.log` (cache trace), and
`matched-{before,after}-{1,2,3}.log` with their PNG directories. The archived
`baseline-driver` uses the original renderer and the same export timing hook.
Earlier `comparison.json` records the superseded engine-wide cache attempt,
not the final layer-only implementation.
