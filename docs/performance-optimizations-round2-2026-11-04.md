# Further performance fixes — November 4, 2026

This second Luna xhigh pass adds six accepted optimizations to the [first pass](performance-optimizations-2026-11-04.md). Measurements use actual controller or CPU-render functions in optimized macOS ARM64 builds. Source, binary, and output snapshots are retained against the live dirty-tree baseline.

| Workload | Before | After | Improvement |
| --- | ---: | ---: | ---: |
| 100,000 Library assets, unchanged filter p95 | 192.06 ms | 124.60 ms | 35.12% |
| Board selection details, 256 selected / 10,000 layers | 9.811 ms | 0.210 ms | 97.86% |
| Board selection admission, 256 IDs distributed across 10,000 layers | 3.821 ms | 0.083 ms | 97.83% |
| Final removal, tiny mark / 1600×1200 pixels | 43.945 ms | 23.459 ms | 46.62% |
| Final removal, 16×16 mark / 640×512 pixels | 97.218 ms | 48.104 ms | 50.52% |
| Six mask-count consumers, 64 sets with pending thumbnails | 25.363 ms | Below timer resolution | Thumbnail work eliminated |

The removal row improvements combine two independently measured changes. The accepted gate was at least 20% target improvement with no protected runtime regression above 10%, exact relevant outputs, bounded storage, and no more than three source attempts per candidate.

## Changes and protected behavior

1. **Library sort keys:** parse metadata capture times and derive basenames once per matching asset, then sort prepared keys. Imported, rating, and visual-search modes keep source-index-only storage. Virtual-copy metadata lookup, invalid dates, date/timestamp fallback, numeric/case-insensitive names, and deterministic ties are tested. Three-process median p95 improves 28–45% at 10,000 assets and 34–42% at 100,000. Peak RSS increases 2.95% (452,034,560→465,387,520 bytes), within the guard; unchanged-result scroll and delegate behavior from pass one remain intact.

2. **Board selected-item membership:** replace repeated linear selected-ID searches inside whole-document scans with a local QSet for selections larger than four. Outputs still follow layer order, primary-board constraints and lock filtering. The full selection-detail result hash is unchanged. The one-item guard regresses 2.38%, inside the cap; eight-item selection improves 59.84%.

3. **Board selection admission:** trim and deduplicate large requests in request order, scan layers once for requested IDs, and filter back into the original order. Storage is O(request size). Inputs up to 64 IDs keep the old path. The large implementation stays out of line using Qt's portable `Q_NEVER_INLINE` macro: the first inline candidate missed the small-selection guard and was superseded. Three interleaved process pairs, each with nine trials, verify clustered and distributed requests, whitespace, duplicates, missing IDs, locks and distinct-primary append order. Final small-selection guards stay within 2.1%; clustered 256-ID admission improves 74.42%. All complete output hashes match.

4. **Removal mask growth:** ordered radius-one and radius-two disk row spans remove rejected square-neighbor checks while preserving traversal order, clamping and boundaries. This stage alone improves the tiny-mark case 41.67%. Other radii retain the original loop. A runtime reference checks both optimized radii at image edges and with clamped weights.

5. **Removal matching:** compute valid candidate-neighbor masks once and target context samples once per target, then visit matching neighbors in the original order. Scoring, support, candidate stride and tie behavior are preserved. This stage improves the compact-mark case another 47.77% relative to the stencil candidate. Added candidate-record payload is bounded by the existing sampler: at most 8,191 records / 131,056 requested bytes on this 64-bit build, plus small stack arrays; allocator overhead and RSS were not measured. Both complete RGBA dumps match the original baseline byte for byte. Signed/HDR values, alpha, outside-support pixels and pre-cancel no-op checks pass.

6. **Mask count projection:** count-only reads reuse grouped mask data without calling thumbnail presentation. Grouping is shared consistently between masks and retouch data. Six badge/property consumers previously made 23,040 thumbnail-cache lookups over 15 trials at 64 sets; the candidate makes zero. Count updates after mutation, empty selections, retouch-first grouping and real presentation remain tested. Candidate batches returned zero at the timer's resolution; this is not a zero-cost claim.

The first pure-presentation timing comparison had unequal warmup: baseline count reads had already presented masks 90 times while candidate counts performed no presentation. Its raw artifacts are retained. A matched guard uses five pure-presentation warmups and 21 timed reads in three interleaved processes per version, with no count calls or event loop. All group/source fingerprints and lookup deltas match; 8/64/256-set medians improve 2.79%/0.87%/0.84%. The saved baseline controller object is selected explicitly, confirmed by its link map.

## Validation and evidence

All **14 focused CTests pass**, including new selection-admission guard cases, all first-pass regressions, existing mask/thumbnail tests and HDR retouch invariants. The Release app build-only check passes for the final source. New optimization/test code builds without warnings; existing unrelated checkout work is preserved.

```sh
ctest --test-dir build/release-macos \
  -R '^(catalog_filter_projection_test|catalog_filter_sort_semantics_test|carousel_projection_test|carousel_selection_admission_(1|8|256)_test|mask_count_projection_test|timeline_multicam_projection_test|timeline_multicam_projection_index_test|geometry_transform_invariants_test|removal_kernel_perf_test|thumbnail_presentation_service_test|mask_interaction_performance_test|hdr_retouch_invariants_test)$' \
  --output-on-failure
./scripts/run-native.sh --release --build-only --jobs 4
```

Local evidence lives under [`perf-artifacts/luna-performance-round2-2026-11-04`](../perf-artifacts/luna-performance-round2-2026-11-04): [Library](../perf-artifacts/luna-performance-round2-2026-11-04/catalog/results.json), [Board](../perf-artifacts/luna-performance-round2-2026-11-04/carousel/results.json), [removal](../perf-artifacts/luna-performance-round2-2026-11-04/engine/results.json), [mask counts](../perf-artifacts/luna-performance-round2-2026-11-04/mask-count/results.json), [matched presentation guard](../perf-artifacts/luna-performance-round2-2026-11-04/mask-count/matched-masksets-results.json), and [final CTest](../perf-artifacts/luna-performance-round2-2026-11-04/ctest-final-retry.log). Commands, source snapshots, binary hashes and rejected-attempt evidence are retained.

## Remaining findings

The broad source-QML workflow driver was repaired to register the current Scene/Depth types and use the visible Selection Toolbox brush control. It now reaches mask routing, brush HUD, stroke/raster completion and adjustment scheduling, but the complete route sweep still times out after that point (`target_events_met=false`). The last offscreen diagnostic records first-use brush feedback around 122 ms, above its budget. This is a signal for native frame-pipeline investigation, not a completed end-to-end qualification or production-startup result.

Board slide/storyboard getters still scan layers per board, but measured around 0.4 ms for 20 boards and 10,000 layers; they were left unchanged. Metadata text-search preparation and multichannel retouch blur allocations remain source-backed candidates requiring separate measurements. These synthetic, single-host gains do not establish native input latency, playback throughput, real-media export performance, or whole-app responsiveness.
