# Performance optimization round 7 — 2026-11-05

This round continues the first brush-frame investigation using Luna xhigh agents. The accepted Round 6 diagnostic visibility guards remain the starting point. No active editor was opened, closed, or restarted.

## Measured bottleneck

The Release compiled-QML phase probe places most first-use delay before `afterAnimating`. Immediate brush rasterization took 0.36–2.83 ms, the matching scene-graph node update took 0.221 ms, and texture creation rounded to zero microseconds. These measurements do not support a texture pooling change for this scenario.

The prior QML trace records two synchronous preset-thumbnail source lookups inside the first-point interval, with 5.513 ms and 4.848 ms of self time. An early native sample of the isolated benchmark independently catches preset thumbnail cache-key work during late QML incubation. The preset browser remains instantiated while the Presets inspector is hidden, so these lookups are avoidable hidden work. Dispatcher timestamps alone do not prove idle time: GUI callbacks run between the recorded dispatcher events.

Evidence: [phase attribution](../perf-artifacts/performance-round7-2026-11-05/brush/attribution.md), [QML trace attribution](../perf-artifacts/performance-round7-2026-11-05/engine/qtd-first-point-attribution.md), and [native first-feedback stack](../perf-artifacts/performance-round7-2026-11-05/review/native-sample-early/first-feedback-stack.txt).

## Preset candidate comparisons

The first candidate guards processed/original thumbnail source bindings with inspector visibility and skips pack-header thumbnail requests. The focused preset browser test passes with no QML warnings: hidden model/draft changes produce zero source lookups, and reopening requests fresh sources and retains search/filter/selection state. Its matched B-C-C-B medians are 78 → 71.5 ms for first point feedback (8.33%) and 81 → 74.5 ms from stroke start (8.02%), below the 10% gate. Every receipt comes from the visible immediate brush item. Warm point p95 rose 48.5 → 59.65 ms (+22.99%), high-fidelity raster p95 rose 97.725 → 118.925 ms (+21.69%), and open-photo p95 rose 143.975 → 154.2 ms (+7.10%). Peak RSS rose 4.36%, within its cap; host load and receipt integrity pass. This candidate is rejected on target and protected latency results. [Exact comparison](../perf-artifacts/performance-round7-2026-11-05/brush/compiled-pairs/comparison.json).

The second candidate additionally suspends the hidden ListView model and skips hidden host row construction. It keeps the browser root mounted, restores a bounded scroll position after reopening, and clears only transient hover preview through the existing host handler. Its expanded UI test passes for draft retention, nonzero scroll restoration, changed-model clamping, rapid hide/reopen, fresh sources, unpinned hover cleanup and pinned preview retention. Matched first-point feedback improves 81.5 → 70.5 ms (13.50%), and stroke feedback improves 84.5 → 73.5 ms (13.02%). High-fidelity raster completion p95 rises 100.325 → 114.65 ms (+14.28%), exceeding the 5% protected limit. Peak RSS rises 1.87%, within its cap. It remains rejected. [Exact comparison](../perf-artifacts/performance-round7-2026-11-05/brush/compiled-pairs/attempt2/comparison.json).

The final source candidate adds visibility gating to `AppIcon` painting and requests a new paint on becoming visible. The existing QML trace records 1,110 icon paint callbacks in the first-point interval (7.009 ms aggregate self time). The icon UI test additionally compares pixels after name/color/size/stroke changes under a hidden parent against a freshly shown equivalent icon, and verifies an icon cleared while hidden reappears blank.

The third candidate passes the predeclared gates and is retained. All four processes completed, every receipt used the visible immediate item (source bit 1), all non-QML link inputs and the strengthened driver are identical, only three QML source files differ, and source hashes remain stable through cache compilation. The icon UI test passes with exact fresh-versus-reopened pixels, all shared icon names at 14/18/24 px and all three interface densities, alongside its existing control checks.

| Metric | Baseline | Retained candidate | Change |
| --- | ---: | ---: | ---: |
| First point to visible scene-graph frame | 80.0 ms | 57.0 ms | -28.75% |
| First stroke start to visible scene-graph frame | 83.0 ms | 60.0 ms | -27.71% |
| Warm point-to-frame p95 | 48.5 ms | 50.05 ms | +3.20% |
| High-fidelity raster completion p95 | 98.475 ms | 103.025 ms | +4.62% |
| Commit-visible p95 | 7.0 ms | 7.0 ms | unchanged |
| Open-photo preview p95 | 142.775 ms | 149.6 ms | +4.78% |
| Brush HUD p95 | 58.35 ms | 60.775 ms | +4.16% |
| Whole-process peak RSS | 2,305,196,032 B | 2,323,415,040 B | +0.79% |

The largest protected increase is +4.78%, within the 5% cap. Both first-use measurements still exceed the existing absolute 50 ms p95 budget. One candidate process also has warm p95 50.4 ms; the retained median of process warm p95 values is 50.05 ms. The improvement does not establish complete brush budget compliance. No clock, receipt timing endpoint, mask resolution, brush pressure/erase behavior, or output quality was relaxed.

Evidence: [final matched plan](../perf-artifacts/performance-round7-2026-11-05/brush/compiled-pairs/attempt3/plan.json), [comparison](../perf-artifacts/performance-round7-2026-11-05/brush/compiled-pairs/attempt3/comparison.json), [source/link identity](../perf-artifacts/performance-round7-2026-11-05/brush/compiled-pairs/attempt3/candidate/link-equivalence.json), [exact retained patch](../perf-artifacts/performance-round7-2026-11-05/verification/accepted-qml-and-tests.patch).

## Benchmark correctness

The workflow receipt now requires a feedback item with positive geometry, effective visibility, and positive opacity throughout its ancestor chain. It retains the existing painted revision/point checks, scene-graph synchronization and subsequent frame swap. A hidden vector Canvas can no longer satisfy the receipt alone. The same strengthened driver and clocks are used by both comparison variants. Each receipt logs its qualifying source mask. Every measured first/warm sample in the first comparison has source bit 1 (visible immediate item). Qualification covers this default, non-auto-mask fixture. The separate raster receipt branch still lacks explicit image-source revision provenance and is not qualified by these measurements. The exact-source visibility probe covers visible, hidden, zero-size, and zero-opacity ancestors.

The earlier phase probe recorded source mask 5: both the immediate item and a hidden vector Canvas. The visible immediate item independently supported that measured frame, so this is a correctness repair for subsequent comparisons, not grounds to claim the earlier frame was false.

## Regression fixture corrections

Two stale tests assumed creating a working selection also created an adjustment layer. Both now assert selection/layer independence and explicitly create a layer before layer feedback/history checks. Selection and mask IDs remain stable through undo/redo, and explicit layer ownership is checked. The original 33 ms feedback limits remain unchanged. Both focused tests pass.

Evidence: [fixture patch, hashes and results](../perf-artifacts/performance-round7-2026-11-05/review/legacy-fixtures/README.md).

## Final verification and preservation

The final Release application, normal workflow driver and all focused targets built successfully. All 17 focused CTests passed in 28.86 seconds: preset browser, icon system, feedback/history fixtures, close coordination, save queue, catalog filter/sort/metadata/culling, native/portable journal recovery, mask interaction, async recipe preparation, brush dispatch/projection, and Timeline lifecycle. The held-worker queue telemetry check also passed (0 ms dispatch, 253 ms pool wait, 4 ms render, 257 ms request-to-completion). The exact-source receipt visibility probe passed.

All monitored QML/controller/engine/test source hashes are stable through the final build, and the eight owned source files still match those hashes after testing. Private instrumentation did not modify the production brush item or header. Existing Round 6 accepted guards and unrelated dirty-tree work remain intact. No active editor was opened, closed or restarted; no commit was created. Rust and a full native displayed-frame workflow were not run because those paths were not changed/available in this proof.

Evidence: [final result](../perf-artifacts/performance-round7-2026-11-05/verification/final-result.json), [CTest log](../perf-artifacts/performance-round7-2026-11-05/verification/final-ctest.log), [build log](../perf-artifacts/performance-round7-2026-11-05/verification/final-build.log), [source hashes](../perf-artifacts/performance-round7-2026-11-05/verification/final-owned-source-hashes.json), and [binary hashes](../perf-artifacts/performance-round7-2026-11-05/verification/final-binary-hashes.json).

The next measured cost is high-fidelity brush result delivery, now 103.025 ms p95 in the retained pair. The first candidate diagnostics suggest GUI delivery can lag worker time: the second candidate's first result was observed at 118–120 ms despite 84 ms worker time. More attribution is needed before changing that path. Physical displayed-frame latency still needs an exposed native-window measurement.

## Qualification

Comparison uses normal Release compiled QML, four workflow iterations per process in B-C-C-B order, median process statistics, both first-use target latencies at least 10% faster, protected mean/p95 at most 5% slower, worst candidate versus best baseline peak RSS at most 10% higher, and host load at most 7. Build and timing share the repository build mutex. Private instrumented probes are attribution evidence only. Offscreen scene-graph receipts do not establish physical displayed-frame latency on a native exposed window.

The [performance-loop skill](/Users/bradyreiner/.codex/memories/skills/photo-editor-perf-loop/SKILL.md) requires: “Revert if the target misses badly or violates a protected guardrail.” The first two candidates remain rejected as standalone results. The third adds a distinct trace-supported paint optimization and passes a fresh B-C-C-B comparison; no failed candidate was accepted by retiming or loosening tolerances. Threshold comparisons use the direct before/after boundary to avoid binary floating-point falsely flagging exactly +5.00% as above 5%.
