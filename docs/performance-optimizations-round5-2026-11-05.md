# Performance optimization round 5 — 2026-11-05

Luna xhigh subsystem agents investigated Library filtering, brush feedback, and checkpoint serialization. The retained production optimization is the Library source-order cache. Checkpoint candidates remain private because they failed protected latency limits. Brush profiling narrowed the cause without producing a qualified fix.

## Retained: Library filtering

`CatalogAssetModel::applyFilters()` reuses capture/filename order for broad results (at least 512 source rows and half the library). It validates current identity, paths, capture metadata/fallbacks, locale, time zone, and sort mode; owns cached strings; limits estimated retained storage to 32 MiB; and keeps the existing sort path as fallback. Text/visual search and unsupported sort modes release unused cached order. Clearing search rebuilds it only when eligible.

The benchmark now includes initial apply, the first query including cache destruction, and clearing search including reconstruction. Two process pairs alternated baseline→candidate then candidate→baseline under the shared build lock. Values are medians of the two process statistics; RSS uses worst candidate versus best baseline.

| 100,000 assets | Baseline | Retained | Change |
|---|---:|---:|---:|
| Initial model apply | 329.066 ms | 254.341 ms | -22.7% |
| Unchanged filters p95 | 164.701 ms | 19.407 ms | -88.2% |
| Same-order edits p95 | 156.245 ms | 19.415 ms | -87.6% |
| Narrow membership p95 | 91.964 ms | 31.272 ms | -66.0% |
| Restore membership p95 | 169.372 ms | 26.691 ms | -84.2% |
| First metadata query | 238.456 ms | 234.521 ms | -1.7% |
| Clear query/rebuild | 169.047 ms | 119.900 ms | -29.1% |
| Query changes p95 | 166.161 ms | 145.455 ms | -12.5% |
| Repeat query p95 | 25.970 ms | 23.761 ms | -8.5% |

Peak RSS: 849,838,080 → 893,829,120 bytes (+5.18%, below the 10% cap). All corresponding 10,000-asset metrics improve. Four benchmark processes passed result/order/QML verification. Focused tests cover mutable-backed strings, capture fallback changes, storage-budget fallback, and cache release/rebuild across text search, visual search, and rating sort.

Evidence: [catalog results](../perf-artifacts/performance-round5-2026-11-05/catalog/round5-result.txt), [scoped patch](../perf-artifacts/performance-round5-2026-11-05/catalog/candidate/catalog-sort-projection-lifecycle.patch). Live catalog implementation hashes were checked against the measured frozen candidate before final compilation.

## Not retained: checkpoint candidates

The Round 4 shared photo-layer JSON array remains in production. Neither Round 5 checkpoint candidate was applied to production.

The predeclared loop used at most three candidates, a 10% target, protected p50/p95 regression no greater than 5%, and RSS growth no greater than 10%. Each candidate ran 64 changed-edit samples per fixture in two alternating process pairs, plus identical large paint checkpoint guards. Raw samples, child RSS, host load, frozen inputs, compile/link commands, maps, binary/source hashes, exact recipe output, and comparisons are retained.

1. Serialize a retouch object once and share it between the two schema aliases: retouch p50 improved 30.1%, but 4 MiB brush p95 regressed 7.36% and 16 MiB tone p95 regressed 5.58%.
2. Additionally remove only the serializer lambda's redundant normalization (preserve whole-stack ID normalization and the outer per-mask normalization): retouch p50 improved 31.6%, but small-tone p50 regressed 5.37%. Other protected matrix p50/p95 metrics passed; large paint median was +0.30%, cold -0.76%, and RSS +6.19%.

Both passed exact-output/reopen checks, including mixed missing IDs, uniqueness, copy-on-write independence, and synthesized legacy linear/radial mask clamping. The same large journal bytes and decoded tiles were preserved. The guard failures prevent acceptance; this pass stops after two distinct candidates because protected timings remain unstable.

The applied [photo-editor-perf-loop skill](/Users/bradyreiner/.codex/memories/skills/photo-editor-perf-loop/SKILL.md) states: “Revert if the target misses badly or violates a protected guardrail.” These private candidates therefore remain unshipped despite target gains.

Evidence: [attempt 1](../perf-artifacts/performance-round5-2026-11-05/checkpoint/comparison.json), [attempt 2](../perf-artifacts/performance-round5-2026-11-05/checkpoint/attempt2/comparison.json), [normalization review](../perf-artifacts/performance-round5-2026-11-05/review/retouch-and-catalog-review.md).

## Brush attribution: no production patch

The frozen normal Release source-QML/offscreen workflow reached its target events, but missed feedback thresholds: first point-to-frame 78 ms; stroke-begin-to-frame 81 ms. First brush mask delivery spent about 225 ms synchronously between mask-state and controls checkpoints. Instrumented selected C++ getters totaled under 1 ms; they do not explain that stall. Binding evaluation, handlers, conversion, or uninstrumented model work remain candidates, not confirmed causes.

A private QML-debug driver connected over loopback, but the bounded capture/flush attempt produced a damaged trace. No profiler result is claimed. Getter instrumentation was removed. The measured QML/controller snapshot is frozen; three QML files changed through parallel work afterward, so attribution is snapshot-bound. These offscreen source-QML results do not qualify packaged compiled-QML behavior or physical displayed frames.

Evidence: [brush attribution and identities](../perf-artifacts/performance-round5-2026-11-05/brush/round5-attribution.md).

## Final verification

Final Release application and focused target build passed. All 13 focused CTests passed: catalog projection/sort/metadata/culling, journal native/portable, save queue, close coordination, async recipe preparation, mask interaction, brush dispatch/projection, and Timeline lifecycle. The isolated held-worker queue telemetry regression also passed. Owned source hashes were stable during final compilation and match the measured catalog candidate. See the [verification logs](../perf-artifacts/performance-round5-2026-11-05/verification/final-ctest.log) and source/binary identity receipts. The Library metadata-persistence guard passed after stale `studio_root` references in the shared two-video test helper were corrected; parallel work further scoped that lookup to `edit`. No unrelated UI/controller changes were reverted.

Known inherited default feedback/history fixtures from Round 4 assume mask creation auto-creates a photo layer; matched baseline/current failures were already recorded there. This pass uses the isolated held-worker queue telemetry regression and does not change selection semantics to satisfy those fixtures.

No active editor session was opened, closed, or restarted. No commit was created. All timing and compilation used the shared Bash build lock. Initial/final dirty inventories and source manifests distinguish this pass from concurrent work.
