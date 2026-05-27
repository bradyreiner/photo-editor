# Performance Baselines

This file records reproducible local performance artifacts. Treat synthetic runs as smoke
baselines only; release baselines still need representative JPEG/HEIC/RAW corpora on target
hardware.

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
