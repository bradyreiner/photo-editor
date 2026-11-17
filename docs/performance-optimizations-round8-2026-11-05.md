# Performance optimization round 8 — 2026-11-05

This round uses Luna xhigh agents to investigate high-fidelity brush result notification after the accepted Round 7 first-frame optimization. The target is the existing `brush_high_fidelity_raster_complete` notification endpoint, not physical displayed-image arrival. The starting monitored source files match the final Round 7 hashes exactly.

## Attribution

A private Release controller probe retains the accepted Round 7 compiled-QML and other C++ inputs, replacing only the instrumented controller object. It timestamps submission, worker entry, raster append, projection completion, watcher callback, publication and signal fanout. Instrumented runs are diagnostic only and are not used to qualify a performance improvement.

| Phase | First point | Later stroke first points |
| --- | ---: | ---: |
| Queue wait | 0.184 ms | 0.127–0.141 ms |
| Raster append | 8.390 ms | 8.245–8.832 ms |
| Projection | 73.954 ms | 49.932–51.289 ms |
| Worker end to GUI callback | 8.803 ms | 7.884–19.411 ms |
| Publication before notify | 0.173 ms | 0.161–0.419 ms |
| Notification fanout | 6.149 ms | 6.487–8.226 ms |

The authoring raster is 1800×1200; the first display projection is 3108×2072 and later projections are 2560×1707. Projection samples the full destination even for a small mark, making it the dominant measured cost. Fresh-image recoloring also scans the whole raster, but at about 8 ms it is a smaller target and remains unchanged in this round.

Evidence: [phase rows and durations](../perf-artifacts/performance-round8-2026-11-05/probe/phases.json), [private compile/link manifests](../perf-artifacts/performance-round8-2026-11-05/probe/).

## Candidate 1: general sparse projection

For inactive geometry and an output at least as large as the converted source, the helper bounds all nonzero premultiplied source pixels, then projects only the destination rectangle whose exact bilinear taps can overlap those bounds. It retains the original coordinate conversion, interpolation, edge extension, rounding, output size, format and transparent fill. Nonzero RGB at zero alpha is conservatively retained. Four nonzero corners establish full source bounds without a scan. Active geometry and downsampling use the full loop.

The candidate passes all exact-image cases and improves workflow high-fidelity mean 86.25 → 54.875 ms (36.38%) and p95 94.6 → 62.25 ms (34.20%). It fails the protected commit-visible average/p95 (+19.61%/+37.91%) and open-photo average/p95 (+5.02%/+11.88%). Peak RSS rises 0.92%, within its cap. A separate identical-harness B-C-C-B projection comparison finds 90–92% sparse gains but a degenerate-axis regression of 9.75% median and 17.34% p95. It is rejected.

Evidence: [workflow comparison](../perf-artifacts/performance-round8-2026-11-05/comparison.json), [projection comparison](../perf-artifacts/performance-round8-2026-11-05/projection/comparison.json).

## Candidate 2: worker opt-in

The sparse path becomes an explicit default-false hint. Only the ordinary, non-auto-mask, non-edge-protected, non-component-sampled brush worker opts in. Synchronous GUI projection keeps the full-loop path. Source images with either dimension equal to one also bypass the sparse scan. This addresses added GUI/degenerate-axis work without changing sampling quality or rescheduling the notification endpoint.

The same expanded projection harness compiles against the frozen preceding header and the candidate header. It checks the complete image against an independent full-loop oracle for empty images, disconnected pixels, fractional scale and nonzero ROI, both image-edge fringes, zero-alpha RGB data, four nonzero corners and sparse downsampling. The original seven dense/downsample/ROI/geometry/identity/degenerate scenarios are retained. Two realistic sparse brush-shaped workloads are timed, with raw nanosecond samples preserved.

The candidate passes these projection guards: 2560×1707 sparse median 48.238 → 4.106 ms (91.49% faster), 3108×2072 sparse median 71.657 → 4.981 ms (93.05% faster). Every protected median/p95 stays within 5%; the largest increase is 3.34%. Full-process peak RSS rises 0.85%, within its cap. All exact-image comparisons pass.

Evidence: [projection plan/results](../perf-artifacts/performance-round8-2026-11-05/projection2/comparison.json).

The second candidate still fails workflow guardrails despite a 37.71% mean improvement (89.5 → 55.75 ms) and 32.21% p95 improvement (96.8 → 65.625 ms). Commit-visible average/p95 rises 9.26%/18.37%, and open-photo p95 rises 5.84%. Workflow peak RSS rises 3.59%. It is rejected as a standalone result. [Workflow comparison](../perf-artifacts/performance-round8-2026-11-05/attempt2/comparison.json).

## Candidate 3: suspend the hidden immediate preview

A second private probe links the accepted C2 controller with the earlier item profiling object. It records 24 hidden `syncFromController()` calls totaling 14.709 ms, up to 0.887 ms per call. The immediate preview continues building and projecting its image while hidden behind the high-fidelity raster. The third candidate adds an effective-visibility early return for the immediate item and rebuilds from current controller points on becoming visible. Hiding clears cached raster/image/point/stroke state so a later stroke with the same point count cannot reuse old coverage. Completed paint receipts remain driven by actual scene-graph painting. The native item path is retained.

The matched final candidate passes all performance gates. The worker/controller object is reused from C2 after exact source-hash verification, and only the item object is additionally replaced. The projection header is byte-identical to the candidate used by the passing C2 micro comparison, so its dense/sparse proof is reused rather than resampled. The same strengthened workflow driver, compiled-QML resources and remaining frozen link inputs are used for both variants.

| Metric | Baseline | Candidate 3 | Change |
| --- | ---: | ---: | ---: |
| High-fidelity result notification mean | 88.875 ms | 57.5 ms | -35.30% |
| High-fidelity result notification p95 | 104.375 ms | 68.85 ms | -34.04% |
| Warm feedback p95 | 50.05 ms | 45.05 ms | -9.99% |
| Commit-visible p95 | 6.425 ms | 6.425 ms | unchanged |
| Open-photo preview p95 | 153.8 ms | 137.825 ms | -10.39% |
| First point-to-frame | 54.0 ms | 56.0 ms | +3.70% |
| First stroke-start-to-frame | 58.0 ms | 59.0 ms | +1.72% |
| Whole-process peak RSS | 2,287,009,792 B | 2,289,188,864 B | +0.10% |

The largest protected increase is 3.70%, within the 5% cap. The first-use point/stroke feedback remains above the absolute 50 ms budget, so this is not complete brush-latency compliance. All timings are the median of process statistics from two alternating pairs, not native physical display proof.

Evidence: [hidden-work probe](../perf-artifacts/performance-round8-2026-11-05/probe-hidden-item/summary.json), [final plan](../perf-artifacts/performance-round8-2026-11-05/attempt3/plan.json), [final workflow comparison](../perf-artifacts/performance-round8-2026-11-05/attempt3/comparison.json).

## Final verification and preservation

The final Release application, workflow driver, test runtime and focused targets build successfully. All 20 focused CTests pass in 41.88 seconds, including brush raster/fidelity, canvas coordinates, the expanded projection oracle, the new immediate-item hide/re-show lifecycle, preset/icon UI, selection/layer history, catalog, recovery/save coordination and Timeline lifecycle. The held-worker queue telemetry check passes (0 ms dispatch, 240 ms pool wait, 7 ms render, 248 ms request-to-completion).

The lifecycle test uses actual `BrushLivePreviewItem` objects in an offscreen `QQuickWindow`. It compares complete captured item pixels with a freshly created item after replacing a stroke with the same point count under a hidden parent and resizing it; it checks ink in the new stroke region, then blank output after hidden clearing and controller destruction. The test target now explicitly links Qt Quick. Existing dispatch-performance cases remain unchanged.

All monitored source hashes remain stable through the final build and tests. Every monitored non-owned source file matches the Round 7 final snapshot; the Round 7 QML optimizations are preserved exactly. The retained patch is limited to the worker projection hint and helper, hidden immediate-item lifecycle, stronger benchmark receipt, expanded projection/lifecycle tests and the test's Qt Quick dependency. No active editor was closed or restarted; no commit was created. Rust, GPU transfer paths and a foreground native display workflow were not changed or qualified.

Evidence: [final result](../perf-artifacts/performance-round8-2026-11-05/verification/final-result.json), [CTest log](../perf-artifacts/performance-round8-2026-11-05/verification/final-ctest.log), [lifecycle test](../perf-artifacts/performance-round8-2026-11-05/verification/lifecycle-test.log), [exact retained patch](../perf-artifacts/performance-round8-2026-11-05/verification/retained-source.patch), [source preservation](../perf-artifacts/performance-round8-2026-11-05/verification/unrelated-source-preservation.json), and [link identity](../perf-artifacts/performance-round8-2026-11-05/attempt3/link-equivalence.json).

## Receipt correctness and proof limits

The benchmark raster feedback branch now requires an advanced revision, a changed nonempty controller source URL, and equality between the ready raster Image source and that current URL. Existing visibility, point-count and scene-graph synchronization/swap checks remain. The immediate/vector branches and all clocks remain unchanged. Both final workflow variants use this same driver. The first exploratory baseline using the preceding receipt is preserved separately and excluded from comparisons.

The high-fidelity target still measures GUI revision notification. It does not establish that the corresponding image has reached a displayed frame. The scene-graph feedback receipts are offscreen; no exposed native window or physical scanout is qualified.

## Gates

At most three distinct source candidates. Accept only a target mean and p95 improvement of at least 10%, all protected workflow means/p95 values at most 5% slower, dense projection medians/p95 at most 5% slower, and worst candidate versus best baseline full-process peak RSS at most 10% higher. Each comparison uses B-C-C-B order and median process statistics, with four workflow iterations per process and host load at most 7. All builds and timings use the shared Bash build mutex. No output resolution, mask fidelity or recovery contract is relaxed.

The [performance-loop skill](/Users/bradyreiner/.codex/memories/skills/photo-editor-perf-loop/SKILL.md) requires: “Revert if the target misses badly or violates a protected guardrail.” Standalone C1 and C2 remain rejected. C3 includes a distinct probe-supported hidden-work fix and passes fresh alternating pairs without changing thresholds or clocks. The final production source is retained only after both exact-pixel/lifecycle checks and all protected performance gates pass.
