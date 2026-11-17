# Performance optimization round 9 — 2026-11-05

This bounded Luna xhigh round investigates the remaining first brush-feedback delay. No production optimization is retained: three distinct candidates fail the predeclared target or protected latency limits. Earlier accepted Round 7 and Round 8 optimizations remain intact.

## Fresh attribution

A private driver records dispatch, animation, scene-graph synchronization and swap phases using the same strict painted-surface receipt. Its first instrumented frame spends 8.840 ms in dispatch, reaches `afterAnimating` at 59.967 ms, synchronization at 63.694 ms and swap at 67.256 ms. These instrumented results are diagnostic only; the normal first baseline reports 57 ms.

The fresh compiled-QML trace uses the normal offscreen rendering path with no software-backend override. Explicit private debug markers bracket the first point at trace time 3.937358–3.990216 seconds. In that 52.858 ms profiler interval, `StudioImageViewport.qml:341` has three width-binding evaluations with 6.307 ms total self time, and `SharedMaskOverlay.qml:630` has a 2.224 ms visible-binding evaluation. Both synchronously trigger native immediate-preview reconstruction. Cursor contour work is smaller: 34 calls totaling 1.084 ms. Hidden icon paint work is down to 0.417 ms despite over 1,200 callbacks, supporting the earlier accepted guard.

The vector Canvas paint occurs while hidden, but its receipt flag only records paint state and does not request painting. Its trigger was not isolated sufficiently to claim a production optimization. It was left unchanged.

Evidence: [frame phases](../perf-artifacts/performance-round9-2026-11-05/phase/phase-summary.json), [QTD attribution](../perf-artifacts/performance-round9-2026-11-05/profiler/qtd-first-feedback-window.json), [capture manifest](../perf-artifacts/performance-round9-2026-11-05/profiler/capture-manifest.json).

## Matching and workspace isolation

A concurrent Debug build and selection changes appeared after the initial source snapshot. Timing paused while host load exceeded 7; no processes belonging to the other work were stopped. Qualified comparisons hold both Release and Debug build locks. Their C++/compiled-QML inputs are frozen from the accepted Round 8 fixture, with identical source-header graphs and driver in each variant. The editor controller header is pinned to that fixture so concurrent selection API additions cannot alter the comparison. Only owned item/controller objects and the corresponding vtable object are replaced where required. These results qualify the frozen fixture, not the concurrent selection feature workflow or physical native display latency.

The initial pre-null-guard baseline and failed load-admission attempts are excluded. Every qualified process completes all target events, uses a visible brush surface, and has before/after host load at most 7. Each comparison uses B-C-C-B order, four workflow iterations per process, median process statistics, and worst candidate versus best baseline peak RSS. Target first-point and stroke-start feedback must each improve at least 10%; protected means/p95 values and initial mask-stack update must stay within +5%; peak RSS within +10%. At most three source candidates are allowed.

## Rejected candidates

| Candidate | First point | Stroke start | Reason rejected |
| --- | ---: | ---: | --- |
| Geometry/visibility updates coalesced with Qt polish | 59.5 → 54.5 ms (-8.40%) | 62.5 → 57.5 ms (-8.00%) | Misses the 10% target; all protected limits pass |
| Also defer brush notifications, preserving end-of-stroke invalidation | 58.0 → 63.5 ms (+9.48%) | 61.0 → 67.5 ms (+10.66%) | Target regression; warm p95 +29.93%, commit/open-photo limits also fail |
| Geometry-only polish plus fresh transparent-raster style initialization | 56.0 → 51.0 ms (-8.93%) | 59.0 → 54.0 ms (-8.47%) | Misses target; warm p95 42.6 → 57.65 ms (+35.33%), commit p95 +8.95% |

The third candidate's premultiplied ARGB remains independently testable, but the latency tradeoff is unacceptable. Its fresh-buffer initialization avoids a full recolor only for ordinary new raster buffers; existing image/style changes keep the full recolor. It is reverted together with both polish candidates. No threshold, timer endpoint, feedback receipt or brush resolution is relaxed, and failed results are not resampled for acceptance.

Evidence: [candidate 1](../perf-artifacts/performance-round9-2026-11-05/comparison.json), [candidate 2](../perf-artifacts/performance-round9-2026-11-05/attempt2/comparison.json), [candidate 3](../perf-artifacts/performance-round9-2026-11-05/attempt3/comparison.json), [rollback hashes](../perf-artifacts/performance-round9-2026-11-05/verification/rollback-proof.json).

## Retained correctness and regression coverage

The benchmark source-URL check now first verifies the raster Image pointer exists. This prevents an absent optional raster surface from being dereferenced when a controller source becomes available. The strict revision/source match, visibility, geometry, point count, painted synchronization and subsequent swap checks remain, and all clocks stay unchanged.

The existing actual-item pixel test gains a visible geometry burst, a fresh-item oracle, pending resize followed by hide/controller destruction, and a same-event end/begin stroke replacement with equal point count. It checks complete captured pixels, new-stroke ink, absence of old coverage and real paint witnesses. The worker result test additionally checks every ARGB pixel against the full alpha/style recolor oracle, including transparent pixels outside the dab. Existing alpha hashes and dispatch timing boundaries remain unchanged. These tests remain compatible with the restored production implementation.

The rollback restores the item header/implementation exactly to Round 8. The controller rollback uses the immediate pre-experiment snapshot, preserving the concurrent selection changes exactly. The sparse-projection helper and earlier QML visibility guards are untouched. No active editor was closed or restarted; no commit was created.

## Final verification

After rollback, both Release and Debug applications build successfully against the current checkout. All 20 focused Release CTests pass in 43.19 seconds; the expanded Debug brush dispatch/lifecycle test also passes (6.71 seconds). The held-worker queue telemetry check passes. The new full-image ARGB oracle and event/geometry lifecycle cases are validated on the restored implementation.

All monitored source hashes stay stable through the final builds. Rollback hashes prove the item header/implementation match the preceding accepted version and the controller implementation matches the exact pre-C3 snapshot containing the concurrent selection work. The retained source patch contains only the optional-raster null guard and stronger regression tests. No performance candidate remains in production.

Evidence: [final result](../perf-artifacts/performance-round9-2026-11-05/verification/final-result.json), [Release test log](../perf-artifacts/performance-round9-2026-11-05/verification/final-ctest.log), [Debug test log](../perf-artifacts/performance-round9-2026-11-05/verification/debug-lifecycle-test.log), [retained patch](../perf-artifacts/performance-round9-2026-11-05/verification/retained-source.patch), [binary hashes](../perf-artifacts/performance-round9-2026-11-05/verification/final-binary-hashes.json).

The [performance-loop skill](/Users/bradyreiner/.codex/memories/skills/photo-editor-perf-loop/SKILL.md) requires: “Revert if the target misses badly or violates a protected guardrail.” It also sets a default ceiling of three attempts. This round stops with the failed production candidates reverted rather than retaining an unqualified speedup.
