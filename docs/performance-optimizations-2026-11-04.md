# Measured Library, Timeline, and geometry improvements — November 4, 2026

Three Luna agents (`gpt-6-luna`, `xhigh`) investigated independent current-source paths. The accepted changes remove model copying/reset work, repeated multicam searches, and unused geometry calculations. All measurements use optimized macOS ARM64 Release builds; build and timing windows did not overlap.

| Workload and metric | Before | After | Improvement |
| --- | ---: | ---: | ---: |
| 100,000 Library assets, unchanged filter p95 | 313.17 ms | 191.20 ms | 38.95% |
| 100,000 Library assets, metadata edit + filter p95 | 311.69 ms | 189.38 ms | 39.24% |
| 4,096 Timeline clips / 2,048 multicam groups, compact projection median | 46.30 ms | 14.30 ms | 69.11% |
| Same Timeline fixture, full projection median | 95.33 ms | 60.92 ms | 36.09% |
| 1920×1280 rotation/skew/scale, production geometry median | 6.854 ms | 4.212 ms | 38.55% |

## Library filtering

`CatalogAssetModel::applyFilters()` now filters and sorts source indexes instead of copying complete asset records before sorting. If asset IDs and order are unchanged, it updates only changed rows and emits `dataChanged` for adjacent changed ranges. Actual membership or order changes retain the existing reset behavior. There is no persistent cache.

Across three independent processes per version, the median process p95 at 10,000 assets improves 22.58→14.83 ms for unchanged filtering. At 100,000 assets, structural narrowing improves 146.39→122.66 ms and restoring the complete result improves 313.61→196.62 ms. Each process records seven samples per phase. One first-candidate 10,000-item p95 outlier (45.69 ms) remains in the raw evidence; subsequent candidates measured 14.73 and 14.83 ms. These are small diagnostic sample sets, not production latency percentiles.

The offscreen software QML GridView proof creates/destroys 49/56 delegates during the baseline unchanged-result search and resets scroll Y from 5000 to 0. Every candidate creates/destroys zero delegates and preserves Y=5000. The dedicated regression also verifies persistent indexes and selected metadata during same-order updates, and correct order/count after structural changes.

Median process peak RSS falls from 558,628,864 to 453,443,584 bytes (18.83%). Temporary projection storage is O(asset count) indexes; complete rows are materialized only when the projection changes.

## Timeline multicam projection

`ClipMapContext` now builds a temporary first-match clip→group/angle index and resolves the active angle once per group. Full and compact projections consume that context. Empty IDs, missing IDs, duplicate membership, and the first duplicate angle retain the original lookup semantics. The hash reserves against actual angle entries and avoids allocation when there are no multicam groups.

The controller probe measures five full and five cache-invalidated compact projections. Eight full/compact parity checks cover playheads before, near, and after a cut, all three audio modes, and the no-multicam path. The no-multicam guard also improves: full 59.71→58.69 ms; compact 14.42→14.09 ms. The helper regression checks every indexed result against the original linear reference and verifies clearing/rebuilding the index.

## CPU geometry

`apply_geometry_corrections()` computes inverse-rotation sine/cosine once per call. It skips per-pixel radial `hypot` only when distortion is exactly zero and chromatic aberration is inactive; sampling and active radial calculations retain their original operations.

The first trig-only candidate improved 17.4% and missed the declared 20% gate. The second candidate, including unused radial-work removal, passes at 38.55%. The direct production-function benchmark uses its real scheduler, two warmups, and nine measured trials. Its 2.46-million-pixel output hash remains `17331018078692787041`.

Eight portable regression cases compare every RGBA float bitwise with a runtime reference retaining the original calculation. Coverage includes ±45° rotation, lens correction, skew/scale, content-aware fill, chromatic aberration on both sides of its threshold, and tiny nonzero distortion. Protected geometry-only driver medians remain unchanged or faster: preview 4→4 ms, full 16→14 ms, settled 27→25 ms (11 samples, integer-millisecond telemetry).

## Validation and reproduction

All four registered focused tests pass in the integrated Release build:

```sh
ctest --test-dir build/release-macos \
  -R '^(catalog_filter_projection_test|timeline_multicam_projection_test|timeline_multicam_projection_index_test|geometry_transform_invariants_test)$' \
  --output-on-failure
```

The accepted gate was at least 20% target improvement, no protected runtime regression over 10%, exact relevant results, and bounded additional storage, with at most three source attempts per candidate. Library and Timeline each needed one source attempt; geometry needed two.

The Release app rebuild passed with `./scripts/run-native.sh --release --build-only --jobs 4` (167 seconds). The app was built without launching or closing the existing editor. The source build emitted the existing `timeline_rig_scene.h:293` signedness warning; new optimization and test code built without warnings.

Raw reports, full source snapshots, binary hashes, the initial dirty-tree inventory, and build/test logs are retained locally under [`perf-artifacts/luna-performance-2026-11-04`](../perf-artifacts/luna-performance-2026-11-04). See the [Library comparison](../perf-artifacts/luna-performance-2026-11-04/catalog/comparison.json), [Timeline report](../perf-artifacts/luna-performance-2026-11-04/timeline/results.json), [geometry report](../perf-artifacts/luna-performance-2026-11-04/engine/results.json), and [CTest log](../perf-artifacts/luna-performance-2026-11-04/ctest.log).

These synthetic controller and CPU measurements do not establish native input-to-frame latency, playback performance, whole-app speedup, or real-photo/export qualification. No QML styling, project format, persisted recipe, or user catalog was changed by these optimizations. Existing unrelated checkout changes were preserved.
