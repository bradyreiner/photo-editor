# App Performance And Operability Plan

This is the app-wide loop for measuring, optimizing, and operating PhotoEditor. It builds on the
existing JSONL observability, benchmark artifacts, and proof gates instead of adding a separate
performance system.

## Goal

Make every important workflow measurable, comparable, and diagnosable:

`launch -> import -> cull -> select -> edit preview -> mask/brush -> export -> timeline/carousel`

Each workflow should answer four questions from one benchmark artifact:

- How fast was the user-visible path at p50, p95, and max?
- Where did queue wait, fallback, cancellation, or cache miss pressure accumulate?
- Which failures, warnings, and over-budget events need attention?
- Which checked-in gate prevents this from regressing?

## Budgets

These are the product budgets to track in run artifacts. The values should be tightened only after
representative local corpora are captured on target hardware.

| Workflow | User-facing budget | Operability signal |
| --- | --- | --- |
| Cold launch | Library usable p50 < 1.5 s, p95 < 3.0 s | `desktop.startup.ready.uptime_ms` |
| Warm launch | Library usable p50 < 600 ms, p95 < 1.2 s | startup event plus cache state |
| Import | Large folder discovery streams without UI stalls | discovery, metadata, DB-write stages |
| Library cull/scroll | 60 fps on large catalogs | frame/jank samples, thumbnail queue wait |
| Asset select | Cached raster first preview p50 < 250 ms | `preview.render.completed` by reason/backend |
| Slider drag | Draft under 50 ms, settled under 250 ms | render mode, dirty domain, queue wait |
| Brush/mask | overlay feedback under 16 ms, composited draft under 80 ms | mask work and render cancellation events |
| Export | Saturate CPU/GPU without starving UI | `job.completed` throughput and backend |
| Timeline/carousel | Scroll/playback stays frame-bound, not IO-bound | media probe/cache/poster/waveform stages |

## Event Contract

Keep the current JSONL format. New workflow events should use this shared field vocabulary:

- `workflow`
- `stage`
- `elapsed_ms`
- `queue_wait_ms`
- `asset_id` or `document_id`
- `reason`
- `interactive`
- `backend`
- `fallback_reason`
- `cache_hit`
- `bytes`
- `thread`
- `cancelled`
- `render_within_latency_budget`

High-cardinality details can remain in JSONL only. Console output should stay compact and only call
attention to failures, cancellations, and over-budget events.

## Suite Runner

Use the app-wide suite wrapper for repeatable local runs:

```sh
scripts/perf/run-app-performance-suite.py --profile smoke
scripts/perf/run-app-performance-suite.py --profile local-dev
scripts/perf/run-app-performance-suite.py --profile release-proof
```

The runner creates one suite artifact under `perf-artifacts/app-suite-*`, runs existing benchmark
and proof tools, and writes:

- `manifest.json`
- `operability-report.md`
- `operability-report.json`
- per-suite command logs under `logs/`

Use `--dry-run` to validate command selection without launching benchmarks.
For corpus-backed suites, the runner creates `perf-corpora/preview-synthetic-v1/manifest.json`
when the default synthetic corpus is missing.

## Profiles

| Profile | Purpose | Suites |
| --- | --- | --- |
| `smoke` | Fast local health check | startup, preview raster, thumbnail cold/warm |
| `local-dev` | Normal optimization loop | smoke plus preview pipeline and color adjustment gates |
| `release-proof` | Pre-release confidence | local-dev plus preview frontier, mask performance, and Metal proof |

Hardware-bound gates such as Metal proof may skip or fail depending on host capability. Do not count
a skipped hardware proof as release evidence.

## Optimization Loop

1. Run the smallest profile that covers the workflow being changed.
2. Compare the new suite artifact against the last accepted artifact for the same profile.
3. Find the first slow or over-budget event, then profile that exact stage.
4. Remove work before making work faster.
5. Keep visual parity/proof gates paired with every preview/export optimization.
6. Promote the new artifact path into `docs/performance-baselines.md` only when it is useful as a
   stable comparison point.

## Next Coverage Gaps

- Add a library culling driver that emits frame/jank and thumbnail queue pressure.
- Add import-stage timings for streaming discovery, metadata extraction, and database writes.
- Add export batch throughput gates grouped by backend and fallback reason.
- Add timeline scroll/playback drivers around probe, poster, waveform, and playback state.
- Add QML frame/binding churn samples for Library, Edit, Timeline, and Carousel.
- Add memory growth checks to suite summaries so large libraries cannot hide unbounded cache growth.
