# Performance optimization round 6 — 2026-11-05

Retained: two visibility guards stop hidden color-diagnostics bindings from blocking brush activation. First mask update time falls 232.5 → 24.0 ms (89.7%), and brush HUD p95 falls 222.425 → 59.850 ms (73.1%) in matched normal Release compiled-QML/offscreen fixtures. No rendering or recovery format is changed. Library typing and paint-journal delta experiments failed protected guards and were restored.

## Accepted: suspend hidden color diagnostics

The compiled-QML profiler trace localizes the first brush activation stall to diagnostic property reads, rather than the selected mask getters investigated in Round 5. In a roughly 250 ms activation window, the `StudioWorkspace` provenance binding accounts for about 197.3 ms inclusive (its JavaScript expression about 196.96 ms self time). The hidden soft-proof status label in `StudioColorManagementPanel` also reads the same getter. The C++ getter resolves color assets, compiles transforms/processors, reads source/color state and cache diagnostics, and assembles a large map. It is notified by the general `controlsChanged` signal.

`StudioWorkspace.qml` now reads provenance only while `diagnosticsOverlayPanel.visible` is true. That panel was already hidden during mask work and the primary tool HUD. The soft-proof status label now reads diagnostics only while actually visible and reuses a visibility-gated property between its text and color bindings. Qt can resolve that property-backed map through multiple native reads while visible; no single-read count is claimed. Showing either label restores a fresh read and visible updates remain subscribed. The getter itself and all color/render operations remain unchanged; opening diagnostics can still incur its existing computation cost.

The private profiler driver keeps `QQmlApplicationEngine` and bound context objects alive inside the successful workflow scope, connects explicitly over loopback, and flushes before cleanup. The 313,647,547-byte XML trace has a complete closing trace element. Debug/profiler timings are attribution only. The accepted speed measurements use non-debug Release binaries.

Evidence: [trace attribution](../perf-artifacts/performance-round6-2026-11-05/brush/instrumented/qml-profiler/trace-attribution.md), [scoped QML patch](../perf-artifacts/performance-round6-2026-11-05/verification/accepted-qml.patch).

## Matched normal Release result

Both variants use identical frozen C++ link inputs and private driver code; only compiled QML resource/cache objects differ. A fresh current-UI baseline was frozen after parallel inspector changes, rather than comparing against the older attribution snapshot. Source hashes were stable across each build, all inputs/binaries are hashed, and baseline/candidate resources are retained. Two alternating process pairs ran baseline→candidate, then candidate→baseline, with four brush iterations per process under the shared build lock. Values below are medians of the two process statistics. Peak memory compares the worst candidate with the best baseline. Host load stayed within the predeclared limit.

| Metric | Baseline | Retained | Change |
|---|---:|---:|---:|
| First mask-stack update | 232.5 ms | 24.0 ms | -89.7% |
| Brush HUD average | 107.375 ms | 55.000 ms | -48.8% |
| Brush HUD p95 | 222.425 ms | 59.850 ms | -73.1% |
| Warm point-to-frame p95 | 49.0 ms | 49.5 ms | +1.0% |
| First point-to-frame | 79.5 ms | 80.0 ms | +0.6% |
| First stroke-begin-to-frame | 82.5 ms | 83.0 ms | +0.6% |
| High-fidelity raster p95 | 100.200 ms | 99.125 ms | -1.1% |
| Commit-visible p95 | 7.0 ms | 7.0 ms | unchanged |
| Peak process RSS | 2,330,312,704 B | 2,324,054,016 B | -0.27% |

Every protected stage average and p95 stays inside the 5% limit; the largest increase is Library-route p95 (+3.60%). The first-use point/stroke feedback still misses the existing absolute 50 ms p95 budget in both variants. This is a brush activation improvement, not a claim that all brush feedback budgets now pass. These tests use compiled QML and offscreen frames; they do not establish physical displayed-frame latency on an exposed native window.

The first narrower candidate (overlay guard alone) also passed, at 241.5 → 61 ms first mask update and 230.575 → 77.650 ms HUD p95. It remains as a frozen backup artifact; the two-label candidate is retained.

Evidence: [final pair plan/results](../perf-artifacts/performance-round6-2026-11-05/brush/compiled-pairs/attempt2/comparison.json), [first candidate](../perf-artifacts/performance-round6-2026-11-05/brush/compiled-pairs/comparison.json).

## Rejected: Library query extension

Three source candidates investigated an ASCII-prefix result mask, with fresh cheap-field matching, metadata/source invalidation, Unicode/deletion/mid-string fallbacks, and preserved visual-search shortlisting. Exactness checks and the final four matched `--verify` processes passed. The final candidate improved 100k query-extension p95 128.70 → 20.61 ms (84.0%), but repeated/filter p95 rose 19.05 → 20.29 ms (+6.50%), exceeding the 5% cap. A prior candidate missed the clear-search cap at +5.05%. No timing rerun or post-hoc tolerance change was used to accept these near misses. All four owned source/test files match their Round 6 baseline hashes. The accepted Round 5 Library optimization remains.

Evidence: [Library report and frozen pairs](../perf-artifacts/performance-round6-2026-11-05/catalog/summary.md).

## Rejected: bounded paint-tile journal deltas

The existing transform-only paint benchmark changes layer position, not tile pixels. A retained benchmark addition supplies a real sparse pixel edit and a dense fallback workload. The sparse fixture changes a valid compressed tile across 33 writes, ends with pixels different from the initial state, retains raw timing samples, reopens exactly, decodes tiles, and measures full-process peak RSS through recovery. The dense fixture changes all tiles. The initial 32-toggle exploratory fixture is diagnostic only because it ended at the original tile.

The corrected baseline sparse write median is about 265 ms: roughly 170 ms payload preparation, 58 ms whole-payload hashing, and typically 20–60 ms atomic write. Every changed tile compacts roughly 197 MB. Three candidates allowed bounded replacements only in existing paint-tile coordinate strings, preserving the old typed delta format, checksums, body/fence flush order and full-compaction fallback. Later candidates improved first-line recovery parsing, released large temporary/base references, and bounded large-delta chains. Tests covered eligible appends, quarter-file size fallback, migration-evidence compaction, exact replay, torn large bodies/fences, retry, and periodic compaction.

The best median improvement was about 46%, but recovery process memory still rose at least 57%, exceeding the 10% cap. The third candidate also failed a protected 4 MiB rejected-workload tail metric. All production codec/journal/header changes and candidate-specific journal test expectations were restored exactly; only baseline-compatible sparse/dense benchmark modes remain. The Round 4 shared layer-array optimization is preserved.

Evidence: [paint/journal report](../perf-artifacts/performance-round6-2026-11-05/checkpoint/summary.md).

## Verification and workspace preservation

The final Release application and all focused targets built successfully. All 13 focused CTests passed (catalog projection/sort/metadata/culling, native/portable journal recovery, save queue, close coordination, async recipe preparation, mask interaction, brush dispatch/projection, and Timeline lifecycle). The isolated held-worker queue telemetry regression also passed. Both [exact-source QML probes](../perf-artifacts/performance-round6-2026-11-05/brush/instrumented/binding-probe/results.md) passed: hidden edits make zero diagnostic reads; opening, visible updates and reopening show fresh data; proof status text and color both update on notifications. The source hashes stayed stable through final compilation, and every rejected production file matches its initial snapshot. These are focused checks; the pre-existing default selection/layer fixture failures documented in Round 4 were not part of this run. Raw logs, scoped patches, source/binary hashes and initial/final dirty inventories are under [the verification artifacts](../perf-artifacts/performance-round6-2026-11-05/verification).

Four stale smoke-helper button references were updated to expand existing inspector groups, matching the current stacked-inspector UI and unblocking compilation. Their snapshots and exact patch are preserved separately. The native visual board workflow was not run. Unrelated parallel QML/controller work was preserved. No active editor was opened, closed, or restarted; no commit was created.

The [performance-loop skill](/Users/bradyreiner/.codex/memories/skills/photo-editor-perf-loop/SKILL.md) requires: “Revert if the target misses badly or violates a protected guardrail.” That is why the faster Library and paint candidates remain unshipped after their bounded three-attempt passes.
