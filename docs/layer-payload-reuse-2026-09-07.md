# Verified retained-layer payload reuse

Follow-up: [copy-free, strict layer admission](layer-admission-2026-09-07.md)
removes the remaining admission-state copy, fixes masked nonfinite detail values
and improves the finite-pixel scan. This document records the preceding checkpoint.

## Measured bottleneck

The preceding minification proof measured cold native publication/readback at
134–159 ms. A repeated-edit probe now distinguishes cold and warm work. The GPU
pixel-buffer cache already avoided re-uploading unchanged layers: warm uploads
were only 6,323,184 bytes versus 216,386,544 cold. Nevertheless, warm publication
still took 117 ms median. The standalone uncached layer-program compile probe
took about 101–119 ms: source validation/state copying, payload copying and
content-identity calculation preceded the successful GPU cache lookup.

## Change

The backend briefly locks the existing cache to obtain a lifetime-pinned,
read-only view of its most recently used shared Metal pixel buffer. The layer
compiler compares every active raster's bytes against that payload. Only an
exact match with the same total length permits reuse of its content identity
and storage. Pointer equality or a recipe revision alone never authorizes reuse.
Metadata, transforms, opacity, clipping, masks and working-space state are
compiled afresh. Source validation remains in place.

`CachedFillLayerPixels` holds both a span and a shared owner; the backend's owner
retains the Metal buffer under ARC. `CompiledFillLayerProgram::pixel_data_view()`
supports either the old owned vector or this pinned payload. The CPU retained
reference also consumes the view. A miss takes the original copy/hash path.

This does not create a second persistent CPU pixel cache or increase the existing
eight-entry / 512 MiB GPU pixel-cache budget. A concurrent eviction cannot destroy
a buffer while a compiled program still holds its owner. As before, live commands
and publications can retain resources beyond cache membership; the cache budget
is not a total-process memory bound. Only the most recent cache entry is considered
for this compile shortcut. No rendered-pixel cache version changes are necessary:
the output bytes are unchanged.

## Bounded performance loop

The `photo-editor-perf-loop` skill governed this work: baseline first, at most
three attempts, at least 20% median warm improvement, at most 10% p95/memory
regression, and exact output preservation. Attempt one passed; no further
optimization changes were stacked. A second run confirmed the accepted change.

Artifact root: `/private/tmp/layer-publish-loop-hake3K/`.

| Measurement | Before | Attempt 1 | Confirmation |
| --- | --- | --- | --- |
| Warm publication median | 117.246 ms | 36.604 ms | 32.421 ms |
| Warm p95 (nearest rank) | 122.331 ms | 46.756 ms | 45.437 ms |
| Cold publication | 150.005 ms | 148.732 ms | 141.443 ms |
| Native peak memory footprint | 1,608,074,944 B | 1,616,037,616 B | 1,616,086,720 B |
| Median improvement | — | 68.78% | 72.35% |

Both candidates pass every gate. Each run has one cold frame and eleven warm
frames; every frame changes layer opacity and produces a distinct output hash.
All twelve full GPU-readback SHA-256 hashes match the baseline exactly, and
upload byte counts are identical. The improvement is avoided host-side payload
work, not reduced upload bytes or a lower-quality render. Peak memory growth is
about 0.5%, within the predeclared 10% cap.

The existing photographic test gained an explicit benchmark mode that emits
JSONL rows. It uses the actual publication API and waits for GPU readback; native
availability is required. The standalone compile probe is outside each timed
publication, but inside the process memory receipt. Input is the existing full
4208x3120 Nexus 6 DNG, working Linear ProPhoto, scale 0.12, rotation 17 degrees,
697x567 canvas. No model inference or full desktop UI runs concurrently with the
comparison. Commands used the same release build and fixture:

```sh
/usr/bin/time -l build/release-macos/tests/photo_layer_sampling_image_test \
  /private/tmp/framehouse-six-raw-source-v3/hdrplus-nexus6-skin-0218/hdrplus-nexus6-skin-0218.dng \
  NEW_ABSOLUTE_OUTPUT_DIRECTORY --metal --scale 0.12 --full-resolution --benchmark-frames 12

python3 -B scripts/perf/compare-layer-publication.py \
  --before /private/tmp/layer-publish-loop-hake3K/before.log \
  --after /private/tmp/layer-publish-loop-hake3K/confirmation.log
```

Logs: `before.log`, `attempt1.log`, `confirmation.log`. Machine-readable gates:
`attempt1-comparison.json`, `confirmation-comparison.json`. The comparison helper
requires ordered cold/warm frames, distinct edit outputs, matching hashes and
uploads, valid timings and a native peak-memory receipt. Comparing the baseline
to itself correctly rejects zero improvement (exit 1), recorded in
`no-improvement-rejected.json`.

## Correctness and build evidence

- New `photo_layer_payload_reuse_test` covers reuse, exact retained output,
  opacity/transform/mask/working-space refresh, in-place mutations of the first
  and last pixels' RGBA channels, disabled layers, nonfinite rejection, reshaped
  metadata, missing owners and payload lifetime after external owners release.
- Ten focused tests pass in 11.15 seconds: `regressions.log`. Besides the new
  test, these cover sampling, photographic sampling, alpha, photographic alpha,
  layer color input, layer LUT parity, canonical working fusion, preview math
  and general render parity.
- Native sampling (3,240 cases) and alpha (4,096 cases plus invariants) pass:
  `native-sampling.log`, `native-alpha.log`. These repeatedly vary raster content
  and rendering state with actual GPU readback. The broader Metal color-pipeline
  test also passes unchanged thresholds: `native-pipeline.log`.
- The real 4208x3120 JPEG source reduction passes all four working profiles,
  independent CPU/retained/native sampling checks, and one-level visible 8/16-bit
  output parity: `jpeg-proof.log`. The RAW repeated-edit outputs are byte-exact
  against the pre-change native baseline. Prior photographic color/quality
  evidence remains in `layer-minification-2026-09-07.md`.
- The desktop and regression targets build; scoped whitespace checks pass.
  Logs: `attempt1-build.log`, `regression-build.log`. The initial engine build
  reports only the existing Metal `fastMathEnabled` deprecation.

## Boundaries and next work

This measures warm layer publication, not whole-app realtime rendering. It
excludes file decode, desktop recipe/source compilation, edit queue wait, other
global color stages and UI presentation. Cold publication remains about 140–150
ms. The uncached compiler still deep-copies admission state and performs full
source validation, and warm reuse still compares all source bytes. Those costs,
foreground interaction, contention/eviction stress and broader catalog/camera
workloads need separate measurement. This test does not prove every concurrent
cache-eviction interleaving or a universal 60 fps guarantee.

The full app goal remains open: diverse RAW calibration, loading/recovery,
embedded model provisioning/inference, temporal 4D and film fidelity, and complete
professional editing tools are not established by this optimization.
