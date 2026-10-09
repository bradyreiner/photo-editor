# Interaction performance fixes — October 6, 2026

Implemented after the [remaining-interaction audit](remaining-interaction-performance-audit-2026-10-06.md). Existing unrelated working-tree edits were preserved. Four measured bottlenecks improved; empty-track publication is narrower, but the full clip projection and native mask painted-frame latency remain expensive.

## Accepted measurements

Matched release-build fixtures, three alternating before/after process pairs. Controller rows use the median of each process's five samples; checkpoint rows use the median of each process's 24-edit p95. The acceptance gate was at least 20% primary improvement, at most 20% protected timing/RSS regression with a 2 ms timing noise allowance, and exact correctness. Builds did not overlap accepted timing runs.

| Workload | Before | After | What the measurement covers |
| --- | ---: | ---: | --- |
| Opacity edit, normalized 10,000-point brush | 194.14 ms | 0.122 ms | Public controller callback with warm geometry/signature reuse; no asset persistence or rendered frame |
| Delete radial mask while retaining that brush | 108.65 ms | 0.027 ms | Public deletion callback after the brush signature was warmed |
| Automatic Timeline checkpoint, 10,000 clips | 408.17 ms | 0.515 ms | GUI-thread capture and dispatch; worker persistence excluded from this timer |
| All-selected Timeline projection, 10,000 clips | 325.80 ms | 50.18 ms | Compact strip models including secondary-selection inline keyframes |
| Unchanged 4 MiB matte checkpoint, changing tone | 19.15 ms | 6.39 ms | Synchronous atomic recovery write p95 |
| Unchanged 16 MiB matte checkpoint, changing tone | 77.21 ms | 50.30 ms | Synchronous atomic recovery write p95 |

The Timeline checkpoint's full durable duration stayed approximately 1.75 → 1.72 seconds at 10,000 clips. That work moved off the GUI thread; total serialization/storage work was not eliminated. Immediate raw clip/marker mutations after dispatch did not change the stored snapshot.

Single-selection Timeline projection stayed 41.09 → 40.30 ms. Peak RSS medians were 638.8 → 436.9 MB for the combined projection/mask probe, 834.8 → 869.2 MB for the Timeline journal fixture, and 919.8 → 920.1 MB for the complete recovery matrix. These are process peaks for their respective fixtures, not per-operation retained-memory measurements.

The recovery matrix's changed-16-MiB-matte p95 stayed 125.73 → 121.54 ms. Its largest median protected increase was the rejected-record scenario, 18.54 → 21.59 ms, within the 20% guard. All nine scenarios reopened exactly.

## Changes

- Mask edits reuse canonical point/stroke arrays when geometry is unchanged. New or changed geometry still uses the ordinary normalizer. Validation and dangling-reference repair consume the already normalized stack. A session-owned cache reuses only signatures computed from an exactly matching source payload, prunes deleted masks, and clears on asset changes. Cold/changed geometry still pays signature construction.
- Timeline recovery and autosave capture immutable edit values on the GUI thread and construct JSON on the storage worker. Existing explicit flushes still wait for durable completion, with unchanged recovery schema and hash chain. Clip arrays and mutable top-level collections are detached from live raw pointers. The full inspector and render projections keep their detailed records.
- Timeline strip selection membership uses a set. Secondary selections expose compact strip fields, animated opacity/scale/gain and complete inline keyframes; the primary clip and clips under the playhead retain full inspector records.
- Video/audio-pair, independent-video, graphics and independent-audio creation publish editorial state without republishing unchanged document identity or media organization. Undo/redo and track notifications were checked for all four paths.
- Photo recovery reuses exact large JSON fragments for common writes with an unchanged matte. Changed geometry retains the original single-buffer serialization path. Cache bytes share the existing 64 MiB budget, release obsolete fragments, and clear after an empty successful checkpoint. The immediate QSaveFile commit, owner permissions, checksum, schema and preservation of failed/corrupt recovery data remain intact.

The first two recovery cache designs were rejected for peak RSS increases of about 40% and 68%. The third design passed. Mask normalization/signature reuse took two narrow passes; Timeline snapshot/projection changes each took one. No native mask paint-to-frame improvement is claimed.

## Verification and native proof

The release desktop app built successfully. Six focused tests passed: mask normalization, layer/mask history, coalesced undo, recipe-save journal, Timeline recovery, and frame scheduling. `timeline_history_benchmark --verify` passed canonical field parity, nested undo/redo, secondary keyframes/animated strip fields, snapshot isolation, and all four track-add notification/undo contracts. Recipe journal tests independently compare exact envelope bytes, including tiny scalar changes, layer/effect removal with a large retained matte, queue role transitions, geometry invalidation, failed atomic writes, empty checkpoints and reopened recovery.

The Timeline recovery stress test initially failed a relative RSS limit after one cold projection. Matched diagnostics reached almost the same absolute RSS before/after (~551/~555 MB). A 24-projection check plateaued and then released memory. The test now warms four complete projections before its four measured projections; its 128 MiB retained-growth, initial 512 MiB, and timing limits are unchanged.

Native Cocoa/Metal before/after workflow drivers both exited successfully with isolated app-data and the same frozen source-QML tree. Adjustment deletion preserved the paint recipe, deletion undo/redo was exact, and exported 1800×1200 RGBA64 pixels had the identical SHA-256 `c560c7225d6f6bc0020f4a4a589072713320ecbf7fcd8d1215746ab360798e14`. Both decoded-video runs were media-ready and processed 48 frames at 293×195 under a 25 Hz input fixture. Candidate photo/Timeline captures were visually inspected. The visible save warning is intentional: this fixture uses a failing catalog helper while keeping local recovery authoritative. This proves workflow/render parity for the fixture, not real-photo quality, 4K throughput or packaged production QML performance.

## Remaining large costs

- Native mask feedback still fails the existing painted-frame gate: matched warm p95 114.2 → 117.0 ms against a 50 ms budget. Brush-page/HUD readiness also remains above its 250 ms budget (280.0 → 304.5 ms). The driver itself completed; these are valid failing performance gates. Scalar-mask speedups do not establish fast Canvas/paint/frame presentation.
- Empty-track addition still reads the whole clip strip. At 10,000 clips a minimal projection subscriber still costs roughly 47 ms; suppressing unrelated notifications does not remove that projection. Incremental/visible clip publication needs a separate bounded change.
- Changed large mattes still require a synchronous full recovery rewrite (~122 ms p95 in the 16 MiB fixture). Explicit large-document Timeline saves still construct JSON synchronously. Cold source probing and minimap/QML work identified in the audit were not optimized here.

## Evidence

[Matched measurements and decisions](evidence/interaction-performance-fixes-2026-10-06/summary.json), [native export/undo/decode parity](evidence/interaction-performance-fixes-2026-10-06/native-parity.json), and [valid failing native brush gates](evidence/interaction-performance-fixes-2026-10-06/native-mask-gates.json). Raw reports, probe sources, command argument arrays, native logs/captures and memory diagnostics are in the same evidence directory. Frozen reference binaries remain in `/tmp/looksy-interaction-fixes-2026-10-06/`; hashes and build verification are recorded alongside the evidence.
