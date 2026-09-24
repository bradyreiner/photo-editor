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

Every entry must include `license_verified: true` plus public source/license fields (`source_url`, `source_page`, `license_url`) or private `license_evidence`. It must also record the exact lowercase SHA-256 digest and byte size. Normal validation re-hashes the asset instead of trusting manifest metadata.

The reproducible public seed corpus is defined in `public-seed-manifest.json`. Its source bytes are pinned before download; the fetch stops if a remote server substitutes or changes an asset. Materialize it locally with:

```sh
python3 -B scripts/perf/fetch-real-image-corpus.py \
  --output /private/tmp/photo-editor-real-image-corpus \
  --force

python3 -B scripts/perf/check-real-image-corpus.py \
  /private/tmp/photo-editor-real-image-corpus/manifest.json \
  --require-agent-cases
```

The seed contains versioned global-edit and approval-gated retouch quality cases
in addition to decode and performance coverage. Retouch cases pin the target
label, operation, normalized region, app-owned artifact checks, proof-pixel
locality, surrounding color/gradient continuity, reconstruction texture, and a
separate human-review state. `agent-real-photo-retouch-quality-v2` intentionally
fails an edit when the target disappears but a halo, crosshatch, texture seam,
or locally mismatched fill remains. The same scorer runs inside the production
draft transaction against a retouch-only baseline: failed proof blocks commit,
restores the original recipe, and records the baseline plus rejection metrics in
the run artifacts. The corpus runner consumes that production report rather
than treating corpus-only scoring as authority. Validate the checked-in case contract
without downloading the images with:

```sh
python3 -B scripts/perf/check-real-image-corpus.py \
  perf-corpora/real-image-required/public-seed-manifest.json \
  --schema-only \
  --seed-manifest \
  --require-agent-cases
```

After building `agent_edit_controller_test`, run every case through the actual
deterministic planner, reversible draft controller, proof renderer, pixel-intent
gate, and protected-region scorer:

```sh
build/dev-macos-debug/tests/agent_edit_controller_test \
  --real-photo-corpus \
  /private/tmp/photo-editor-real-image-corpus/manifest.json \
  /private/tmp/photo-editor-real-image-corpus/agent-quality-report.json
```

Run one case during a bounded quality or performance loop with:

```sh
build/dev-macos-debug/tests/agent_edit_controller_test \
  --real-photo-corpus \
  /private/tmp/photo-editor-real-image-corpus/manifest.json \
  /private/tmp/photo-editor-real-image-corpus/agent-retouch-report.json \
  --case-id sunset-remove-upper-right-cloud
```

The generated `agent-real-photo-corpus-report-v1` JSON keeps machine results,
before/after proof paths, exact measured deltas, protected-region results, and
the human-review state separate. Machine success does not approve photographic
taste. The release gate must also require completed human ratings:

```sh
python3 -B scripts/perf/check-real-image-corpus.py \
  /private/tmp/photo-editor-real-image-corpus/manifest.json \
  --require-agent-cases \
  --require-human-ratings

build/dev-macos-debug/tests/agent_edit_controller_test \
  --real-photo-corpus \
  /private/tmp/photo-editor-real-image-corpus/manifest.json \
  /private/tmp/photo-editor-real-image-corpus/agent-quality-report.json \
  --require-human-ratings
```

Both strict commands intentionally fail while any case is `pending` or has a
human `fail` rating. Record the reviewer, ISO-8601 review time, 1–5 rating, and
notes directly in the private/materialized manifest; do not claim the human gate
from machine measurements alone.

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
