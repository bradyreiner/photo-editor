# Real-Image QA Corpus

This directory is the checked-in contract for the real-image QA corpus. Do not commit large licensed customer, stock, or personal photo files here. Store those files in a private local folder, or materialize the public seed corpus into `/private/tmp`, and validate a manifest with:

```sh
python3 -B scripts/perf/check-real-image-corpus.py /path/to/real-image-corpus/manifest.json
```

The manifest must include licensed or internally owned photos covering:

- `dark_raw`: night or underexposed RAW recovery
- `clipped_highlights`: high-contrast scene with clipped or near-clipped highlights
- `smooth_sky`: smooth sky, studio sweep, or other gradient-sensitive region
- `skin`: portrait skin tone under realistic lighting
- `foliage`: landscape foliage/green separation
- `textured_detail`: high-frequency texture, hair, fabric, rock, or high-ISO detail

Every entry must include `license_verified: true` plus public source/license fields (`source_url`, `source_page`, `license_url`) or private `license_evidence`.

The reproducible public seed corpus is defined in `public-seed-manifest.json`. Materialize it locally with:

```sh
python3 -B scripts/perf/fetch-real-image-corpus.py \
  --output /private/tmp/photo-editor-real-image-corpus \
  --force

python3 -B scripts/perf/check-real-image-corpus.py \
  /private/tmp/photo-editor-real-image-corpus/manifest.json
```

Use the same manifest with the benchmark drivers before calling tone, color, mask, thumbnail, or preset work complete:

```sh
python3 -B scripts/perf/run-benchmark.py \
  --scenario perf-preview-raster \
  --app build/dev-macos-debug/tests/preview_benchmark_driver \
  --corpus /path/to/real-image-corpus/manifest.json \
  --event benchmark.preview_corpus.completed \
  --timeout 90 \
  --settle-seconds 0 \
  -- \
  --corpus /path/to/real-image-corpus/manifest.json \
  --limit 12 \
  --timeout-ms 60000

python3 -B scripts/perf/run-benchmark.py \
  --scenario perf-thumbnail-cold-warm \
  --app build/dev-macos-debug/tests/thumbnail_benchmark_driver \
  --corpus /path/to/real-image-corpus/manifest.json \
  --event benchmark.thumbnail_corpus.completed \
  --timeout 90 \
  --settle-seconds 0 \
  -- \
  --corpus /path/to/real-image-corpus/manifest.json \
  --limit 12 \
  --window-size 8 \
  --timeout-ms 60000
```

Record the artifact path and any rejected visual findings in `docs/performance-baselines.md` or the relevant release checklist.
