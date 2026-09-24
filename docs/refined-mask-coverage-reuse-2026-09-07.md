# Exact refined-mask coverage reuse

Status: the third bounded-loop candidate is accepted. Repeated local edits now
reuse refined coverage with unchanged rendered pixels. This is CPU preview
worker acceleration, not whole-editor realtime or color-engine promotion.

## Ownership and invalidation

The local-mask render stage acquires an immutable input/edge snapshot before
editing pixels. Reuse compares the actual RGBA pixel bytes in cancellation-aware
chunks, dimensions, source coordinate mapping, full working context and
photographic rendering model. It does not trust mask IDs, asset IDs, pointers,
quantized alpha or a probabilistic image hash.

Each coverage entry retains the complete mask-set prefix. Only the current
set's known local color adjustment values are neutralized in the key; all other
settings remain. In particular, every previous mask's adjustments and
decontamination settings remain dependencies because downstream range masks see
the previously edited pixel. Source raster/depth/brush/polygon payloads,
composition, parent references, geometry and refinements are compared through
complete value equality. Future fields participate automatically rather than
being silently omitted by a coverage-only whitelist.

Coverage is full-precision double and uses the original per-pixel evaluator.
The first use records a small probation entry and computes pixels normally.
The second use records coverage while rendering; subsequent identical coverage
requests reuse it. This avoids allocating a large weight buffer for a one-off
edit. Buffers are not zero-filled: every pixel is written before publication,
and canceled/incomplete buffers are never published. Published entries are read
only; shared ownership pins them for concurrent readers and through eviction.

The cache has one current input snapshot and up to eight coverage/probation
entries. A shared 384 MiB reservation budget accounts for input/edge buffers,
weight buffers and estimated key/object bytes, including retired entries still
held by active renders. Keys above 32 MiB are not retained. This is a cache
payload budget, not an allocator/RSS or total application memory guarantee.
Allocation/budget failure takes the unchanged evaluator path. Large images,
multiple large masks and concurrently pinned work may exceed cache admission;
those cases remain correct but may not accelerate. No cache is persisted.

## Bounded performance proof

The performance-loop skill set at most three attempts, at least 15% lower warm
median, unchanged ordered output hashes, warm p95 within +5%, first-mask latency
within +20%, and host peak RSS within +10%. The same 24 temperature edits use the
actual preview request entry point and default background-mask edge cleanup.
The rightmost quarter must match neutral exactly on every render, and every
edit must produce a distinct image. Hashing and region checks are outside the
render timer. Native host `/usr/bin/time -l` supplies memory evidence.

"First mask" follows the neutral decode/render; it is **not cold RAW import**.
Warm statistics cover edits 2–24, including the cache-building second edit.
No expensive cache-build frame is omitted from the warm series.

| Metric | RAW before | RAW accepted | JPEG uncached | JPEG accepted |
| --- | --- | --- | --- | --- |
| Dimensions | 4208x3120 | 4208x3120 | 1600x1186 | 1600x1186 |
| First mask ms | 1154 | 1210 | 194 | 152 |
| Second edit/cache build ms | 1324 | 1215 | 168 | 143 |
| Warm median ms | 1183 | 663 | 168 | 71 |
| Warm p95 ms | 1331 | 723 | 209 | 75 |
| Warm maximum ms | 1340 | 1215 | 222 | 143 |
| Peak resident bytes | 2501214208 | 2606514176 | 1579466752 | 1622016000 |
| Median improvement | — | 43.96% | — | 57.74% |

All 24 ordered RGBA SHA-256 values match in both comparisons. RAW peak RSS grows
4.21%; JPEG grows 2.69%. The accepted RAW sequence shows two coverage misses
followed by 22 hits and 315,097,681 accounted retained bytes. JPEG uses the same
engine binary with the test driver's `--uncached-coverage` switch as its control;
there is no production rendering environment-variable bypass.

The first direct-retention candidate improved RAW warm median 45.82%, but its
first mask was 1427ms (+23.7%), failing the gate. Inlining weight access and
removing zero-fill still yielded 1441ms cold and failed. The third candidate's
two-use admission passes every gate. Previous standalone dispatch/distance
experiments remain rejected; see `raw-mask-profile-2026-09-07.md`.

Evidence roots and comparisons:

```text
/private/tmp/mask-coverage-before-ysDO1B
/private/tmp/mask-coverage-after-9ewDCm
/private/tmp/mask-coverage-cold-fixed-9za9wE
/private/tmp/mask-coverage-probation-A8R9JA
/private/tmp/mask-coverage-accepted-comparison.json
/private/tmp/mask-coverage-jpeg-n74u5d/uncached
/private/tmp/mask-coverage-jpeg-n74u5d/cached
/private/tmp/mask-coverage-jpeg-comparison.json
/private/tmp/mask-coverage-gate-self.json
```

## Regression and runtime proof

- `mask_coverage_cache_test`: 38 byte-identical cached/uncached invalidation
  cases, four working spaces and three photographic models with signed/HDR
  source pixels, prior-mask luminance ranges, four concurrent readers,
  changed external edge references, cancellation of a partially written
  coverage lease, and exhaustion/release of the shared in-flight budget.
  The initial working-space test accidentally selected the default ProPhoto
  profile again; its baseline is now explicitly sRGB, so the mutation is real.
- Six focused regressions pass in 4.36s: coverage cache, 1,135,872 compiled local
  adjustment comparisons, color pipeline, preview math, and legacy/ACES 2
  reference workflows. `/private/tmp/mask-coverage-final-tests.log`.
- Controller sampling passes on synthetic P3 JPEG plus the real RAW/JPEG pair,
  four working spaces, cropped/uncropped, exact alpha/color, stale-result
  rejection, notify-driven publication and seven raster modes.
  `/private/tmp/mask-coverage-controller-real-images.log`.
- Desktop builds and the isolated native editing smoke test passes: load RAW,
  place JPEG via QML, publish preview/color-managed thumbnail, undo/redo and
  export. Receipt and inspected screenshot are in
  `/private/tmp/mask-coverage-app-3XXfWi/`. This is a shell/editing regression,
  not a measurement of the new cache's foreground input-to-photon latency.
  Its unavailable-catalog banner is intentional in the isolated closed-port QA
  profile. User catalog and model weights are untouched.

Reproduce:

```sh
QT_QPA_PLATFORM=offscreen /usr/bin/time -l build/release-macos/tests/legacy_preview_locality_driver SOURCE ROOT/output aces2 0 --benchmark-local
python3 -B scripts/perf/compare-repeated-local-edit.py BEFORE_ROOT AFTER_ROOT
```

Write each process's stdout/stderr to its sibling `ROOT/render.log`. Each output
directory must be new/empty. Add `--uncached-coverage` for an unchanged-evaluator
control. The comparator rejects a baseline-to-itself comparison for zero speedup.

## Remaining goal boundaries

The mask stage is faster, but the accepted full-resolution worker median is
still 663ms. Camera characterization/output costs, cold loading, native
foreground interaction, large-mask/cache-pressure workloads, diverse RAW
colorimetric truth and visible RAW noise remain open. No claim is made that
full Photoshop tools, Gaussian 4D/film fidelity, embedded inference/search or
agentic workflows are complete.
