# Performance Baselines

- September 10: [Live draft feedback and hidden scopes](live-draft-feedback-2026-09-10.md): synthetic first-frame median 560→205 ms; nine presented updates during a drag that previously published none. RAW also presents intermediate frames with unchanged final pixels. GUI stalls and longer total burst work remain explicit limitations.

- September 10: [UI identity reuse and prepared advanced layer tone](ui-advanced-tone-performance-2026-09-10.md): advanced-layer median 38.86→9.94 ms with exact pixels; repeated native QML first-frame latency 3960→966 ms and worst drag setter 206→20 ms. Large-mask UI stalls improve, while 490–525 ms heartbeat gaps remain.

- September 10: [Cached zoom, RAW reuse and incremental Library updates](zoom-raw-library-performance-2026-09-10.md): median cached patch 433→312 ms; 30 metadata updates 683→1 ms; compatible RAW decode reuse with exact pixel parity.

## Larger drafts, shared adjustments and virtualized Library, 2026-09-10

RAW draft caps increase from 256/512 to 1024 pixels. Five native color controls
retain draft/reference and preview/export parity. Standard light/color layer
work falls from 114.84 to 5.08 ms in a matched scalar/production benchmark.
A 2,000-item Library layout falls from 1,935 to 59 ms with 24 live tiles instead
of 2,000. Full rendering now consistently yields to pending interactive work.
Heavy RAW drafts still exceed 100 ms, and the broad catalog test hits an import
timeout; seven focused regressions pass. See [evidence and limits](render-drafts-library-performance-2026-09-10.md).

## Layer workflow reuse and neutral work elimination, 2026-09-10

Three alternating native RAW runs per build reduce median full curve-edit render
from 2,983 to 1,957 ms (34.39%), full selection by 6.55%, and actual flattened
export by 6.59%. Median peak RSS falls 3.08%. Draft/reference/full PNGs are
byte-identical across all six runs. Focused scalar comparisons reduce neutral
layer work from 60.10 to 4.69 ms and active layer-curve work from 159.50 to
5.26 ms. A separate full-image curve stress run falls from 32,838 to 4,076 ms
with byte-identical PNGs; flattened export falls from 52,710 to 19,502 ms.
These are local measurements on two recipes; cold workspace and first
brush-commit budget failures remain separate. See [the evidence and limits](workflow-mask-reuse-2026-09-10.md).

## Single startup catalog resolution, 2026-09-10

Removing the second managed-service resolution reduces controlled readiness
median from 666 to 347 ms (47.90%), with peak RSS 0.65% lower. Five probes now
resolve the managed environment five times instead of ten, retain all five health
queries, and admit background work in 0 ms. Authority and failure-path regressions
pass. Both previously failing UI timing checks pass on the fresh baseline without
relaxed assertions. These are readiness-phase measurements, not complete cold
startup or interaction percentiles. See
[the evidence](catalog-readiness-resolution-2026-09-10.md).

## Legacy RAW output batches, 2026-09-10

The matched native CR2 curve-edit run reduces full render time from 6,278 to
4,723 ms (24.77%) and display conversion from 2,118 to 531 ms (74.93%). Full
photo-selection rendering improves 20.69%; peak RSS increases 0.51%. All three
output PNGs are byte-identical and native preview/export parity stays within
1/255. The first candidate passes the 20% target and 10% latency/memory guards;
a second mask-scheduling experiment misses its gate and is reverted. These are
individual measurements under host load, not interaction latency percentiles.
See [the evidence and reproduction notes](legacy-output-performance-2026-09-10.md).

The same investigation replaced the full job-history startup probe with a small
readiness response: 7,056 to 2,595 ms (63.22%), 47.63 to 11.29 MB peak RSS, and
7.56 MB to 15 bytes of output, with the stored job payload unchanged. Managed
startup also avoids a duplicate postmaster start after a short SQL probe fails.
Rust workspace, authority-rejection and bootstrap checks pass; preview-feedback
and recovery-heartbeat timing checks remain failing under the observed host load.

## Startup job recovery, 2026-09-08

Matched delayed-helper recovery admission improves from 445 ms to below 1 ms,
with 29 heartbeat ticks rather than one. Real PostgreSQL recovery takes 1201 ms
in the background with 120 ticks and preserves paused/canceled job states.
The native Scene/import/Edit proof retains exact output files and save/undo;
whole-proof time is 1.50% higher and peak footprint 0.16% higher, within the
predeclared 10% guards. The first bounded-loop implementation is accepted.
Six focused tests pass; three separate history/model failures remain. See
`background-job-startup-recovery-2026-09-08.md`. This removes foreground recovery,
not all job-store waits or app-wide observation gaps (still up to 1206 ms).

## Idle job queue observation, 2026-09-08

The first bounded-loop candidate moves queue reads off the foreground thread:
delayed-helper admission improves from 464 ms to below 1 ms, with 28/29 heartbeat
ticks rather than one. The matched native Scene/import/Edit proof preserves
exact exported files, final outlines and save/undo; total time changes from
16256 to 16003 ms and peak memory slightly decreases. Six of nine broader
checks pass, with three history/model failures explicitly retained. See
`idle-job-queue-responsiveness-2026-09-08.md`. Observation-only gaps still reach
1196 ms; this is not whole-app realtime acceptance.

## Small-image final outlines, 2026-09-08

Matched foreground Scene-to-Edit proofs reduce add-mask admission from 1442 to
74 ms and masked exposure from 1422 to 53 ms. Final outline completion, exact
exported files, catalog save and undo pass; peak footprint increases 4.54%.
The bounded loop accepts its third candidate after correcting legacy local-mask
fields that destabilized the global-color signature. Six focused regressions
pass. See `small-mask-outline-responsiveness-2026-09-08.md` and
`scripts/perf/compare-scene-mask-proof.py`. Later phase inspection attributes the
1565 ms whole-proof gap to the harness's after-capture AI self-check; the maximum
observation-only gap was 1466 ms. These are individual measurements, not
app-wide realtime or p95 acceptance.

## Layer-inspector redo, 2026-09-08

Two foreground native RAW/JPEG app proofs reduce source-layer redo admission
from 1079ms to 56/55ms through a shared reactive presentation snapshot. Export
bytes and inspector undo/redo match; whole-proof time improves and peak memory
stays within +0.73%. The bounded loop accepts its first code candidate; an
inactive-window observation fails and is excluded explicitly, not called a pass.
Three focused tests and new-component qmllint pass. See
`layer-inspector-snapshot-2026-09-08.md` and the deterministic
`scripts/perf/compare-layer-history-proof.py` gate. Whole-proof observation gaps
still reach 1020ms; these are two observations, not whole-app realtime or p95.

## Warm asset loading, 2026-09-07

Moving cached-preview decoding off the GUI thread reduces full-resolution warm
selection admission from 121.68 to 2.37ms RAW and 60.24 to 0.86ms JPEG. Preview-ready
median remains about 125/60ms, all twelve output hashes match, and peak RSS stays
within 0.2% of baseline. The first candidate passes the bounded latency/memory
gates, held-queue/latest-selection regressions, seven focused tests and native
editing smoke. See `warm-asset-loading-2026-09-07.md` for host artifacts and
reproduction. This is controller admission responsiveness, not cold loading or
display input-to-photon latency.

This file records reproducible local performance artifacts. Treat synthetic runs as smoke
baselines only; release baselines still need representative JPEG/HEIC/RAW corpora on target
hardware.

For PRs that claim an optimization, use
`docs/performance-optimization-pr-evidence.md` so the metric, before/after
values, workload, artifact paths, and risk are recorded consistently.

## 2026-09-07 Refined Mask Coverage Reuse

The accepted two-use cache improves full-resolution RAW repeated-local-edit
median from 1183 to 663ms (43.96%) and 1600-edge JPEG from 168 to 71ms (57.74%).
All 24 output hashes match in each pair; peak RSS grows 4.21%/2.69%. First-mask,
second-edit/cache-build, p95 and memory gates pass. See
`refined-mask-coverage-reuse-2026-09-07.md` for the three-attempt loop, exact
invalidation/ownership contract and evidence. These are CPU preview-worker
measurements, not foreground realtime or cold RAW loading acceptance.

## 2026-09-07 Warm Retained-Layer Publication

Follow-up admission optimization: `/private/tmp/layer-admission-loop-EE2des/`
reduces the refreshed 31.9465 ms warm median to 22.7820 ms (28.69%), with exact
GPU output hashes and effectively unchanged peak memory. Cold publication stays
about 140 ms. The bounded loop accepts its second attempt; eleven focused and
native Metal regressions pass. See `layer-admission-2026-09-07.md`. These remain
component timings, not foreground realtime acceptance.

Native full-resolution RAW-source repeated-edit comparison:
`/private/tmp/layer-publish-loop-hake3K/`. Warm publication median improves from
117.246 ms to 36.604 ms; a confirmation run measures 32.421 ms. All twelve GPU
readback hashes and upload counts match; peak footprint increases about 0.5%.
The bounded loop accepts its first attempt against predeclared timing, memory
and correctness gates. See `layer-payload-reuse-2026-09-07.md` for reproducible
commands, machine-readable comparisons and regression evidence. This is a
697x567 warm layer publication with a full RAW source, not whole-editor realtime
or cold-loading acceptance.

## 2026-09-07 Local Recipe Checkpoints

`editor_recipe_save_benchmark` measures atomic local checkpoint writes with a
changed edit on every iteration and an exact fresh-reopen requirement. Two-run
baseline: `/private/tmp/framehouse-checkpoint-baseline-ZLGLIH/`. Accepted two-run
candidate: `/private/tmp/framehouse-checkpoint-candidate2-fixed-cy64AY/`.
Using worst candidate p95 against best baseline p95, 1 MiB tone/brush and 4 MiB
tone scenarios improve 71.5%, 68.6% and 66.1%; small-recipe timing also improves.
Peak RSS decreases 62.5% in this isolated benchmark. Seven focused tests and
real large JPEG/DNG crash/retry/canonical-pixel checks pass.

See `recipe-checkpoint-performance-2026-09-07.md` for commands, stage diagnosis,
raw artifact paths, format compatibility, guards and limits. This is a local
checkpoint baseline, not whole-app memory or foreground rendering acceptance.

## 2026-05-24 Synthetic Raster Preview Smoke

Command:

```sh
python3 -B scripts/perf/create-preview-corpus.py \
  --output /private/tmp/photo-editor-preview-benchmark-driver-smoke \
  --count 2 \
  --force

python3 -B scripts/perf/run-benchmark.py \
  --scenario perf-preview-raster \
  --app build/dev-macos-debug/tests/preview_benchmark_driver \
  --output-root /private/tmp/photo-editor-perf-driver-test \
  --corpus /private/tmp/photo-editor-preview-benchmark-driver-smoke/manifest.json \
  --event benchmark.preview_corpus.completed \
  --timeout 30 \
  --settle-seconds 0 \
  -- \
  --corpus /private/tmp/photo-editor-preview-benchmark-driver-smoke/manifest.json \
  --limit 2 \
  --timeout-ms 15000
```

Artifact:

`/private/tmp/photo-editor-perf-driver-test/20260524T222706.207813Z-perf-preview-raster-90d421a0`

Summary:

- `benchmark.preview_corpus.completed`: 2 assets, 2815 ms
- `benchmark.preview_asset.completed`: p50 1406.5 ms, p95 1659.8 ms, max 1688 ms
- `preview.render.completed`: 4 renders, p50 432.5 ms, p95 819.6 ms, max 875 ms
- `preview.histogram.completed`: 2 runs, p50 64.0 ms, max 65 ms
- `preview.render.completed` backend: CPU with `metal_unavailable` fallback in this environment

## 2026-05-24 Synthetic Thumbnail Cold/Warm Smoke

Command:

```sh
python3 -B scripts/perf/create-preview-corpus.py \
  --output /private/tmp/photo-editor-thumbnail-benchmark-corpus \
  --count 8 \
  --force

python3 -B scripts/perf/run-benchmark.py \
  --scenario perf-thumbnail-cold-warm \
  --app build/dev-macos-debug/tests/thumbnail_benchmark_driver \
  --output-root /private/tmp/photo-editor-perf-thumbnail-test \
  --corpus /private/tmp/photo-editor-thumbnail-benchmark-corpus/manifest.json \
  --event benchmark.thumbnail_corpus.completed \
  --timeout 30 \
  --settle-seconds 0 \
  -- \
  --corpus /private/tmp/photo-editor-thumbnail-benchmark-corpus/manifest.json \
  --limit 8 \
  --window-size 6 \
  --timeout-ms 15000
```

Artifact:

`/private/tmp/photo-editor-perf-thumbnail-test/20260524T224725.515908Z-perf-thumbnail-cold-warm-b1bf01d0`

Summary:

- `benchmark.thumbnail_corpus.completed`: cold visible window ready in 1739 ms, warm visible window ready in 11 ms
- `thumbnail.completed`: 6 cold visible-window thumbnails, p50 809.0 ms, p95 856.8 ms, max 868 ms
- `thumbnail.completed` queue wait: p50 269.0 ms, p95 829.0 ms, max 845 ms
- Thumbnail queue priority mix: selected=1, filtered=2, visible_window=3
- `catalog_local_db` helper emitted a local permission warning in this sandbox; the benchmark driver still completed with `target_events_met=true`.

## 2026-05-24 Color Adjustment CPU Pass Smoke

Command:

```sh
python3 -B scripts/perf/run-benchmark.py \
  --scenario perf-color-adjustment \
  --app build/dev-macos-debug/tests/color_adjustment_benchmark_driver \
  --output-root /private/tmp/photo-editor-perf-color-adjustment \
  --event benchmark.color_adjustment_suite.completed \
  --timeout 90 \
  --settle-seconds 0 \
  -- \
  --iterations 3
```

Artifact:

`/private/tmp/photo-editor-perf-color-adjustment/20260525T033636.373139Z-perf-color-adjustment-55f72ac1`

Summary:

- `benchmark.color_adjustment_pass.completed`: 3 pass sizes, p50 1925.0 ms, p95 6472.7 ms, max 6978 ms.
- `tiny-grid`: 64 x 64, 3 iterations in 26 ms, 8.7 ms/pass.
- `preview-960`: 960 x 640, 3 iterations in 1925 ms, 641.7 ms/pass.
- `full-1920`: 1920 x 1280, 3 iterations in 6978 ms, 2326.0 ms/pass.
- `benchmark.color_adjustment_suite.completed`: 9052 ms total.
- This is a CPU-path smoke baseline after replacing per-pass thread creation with the persistent color adjustment scheduler. It is a scheduler/pass-cost baseline, not a visual-quality baseline.
