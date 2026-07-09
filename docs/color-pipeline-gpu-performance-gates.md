# Color Pipeline GPU And Performance Gates

Color pipeline changes must keep three kinds of evidence visible in perf and
release runs:

- preview latency for representative color edits,
- backend choice for each measured render,
- cache behavior and GPU fallback reason when the render uses CPU instead of
  the Metal path.

## Required Gates

- `render_parity_test` owns CPU/GPU shape correctness. It verifies the supported
  LUT-backed Metal path, named CPU fallback reasons, and CPU/Metal parity for a
  GPU-compatible wide-gamut recipe when a Metal backend is available.
- `scripts/perf/run-preview-release-proof.py` is the release proof wrapper. Its
  report must be kept with the release artifact and includes the preview app,
  color app, parity app, Metal iterations, timeouts, gate status, and smoke flag
  status.
- `scripts/perf/run-app-performance-suite.py --profile release` is the app-wide
  performance wrapper. Use it after color-pipeline changes that affect render
  scheduling, cache keys, output conversion, backend choice, or preview
  publication.

## Representative Color Recipes

Perf runs should include at least:

- a GPU-compatible wide-gamut recipe with `Linear ProPhoto RGB`,
- a global tone/color recipe that is expected to report `exact_global_tone` or
  `exact_post_color`,
- a mask-set recipe that is expected to report `local_mask_sets`,
- an HDR/scene-linear recipe that is expected to report
  `extended_range_pixels`.

Do not treat a CPU fallback as a failure when the fallback reason is expected
and measured. Do treat an empty fallback reason on a CPU render, an unsupported
GPU shape without a reason, or missing backend/cache fields in perf artifacts as
a release-blocking observability regression.
