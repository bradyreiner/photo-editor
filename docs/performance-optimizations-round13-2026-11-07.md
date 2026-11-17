# Performance round 13 — November 7, 2026

Retained changes: oversized clipboard rejection uses 51.2% less encoder CPU and 22.8% less first-call peak memory; unavailable sources no longer launch needless waveform workers; verified export delivery and late-cancel metadata are repaired. ProRes, idle and additional cache candidates were evaluated and left out after failed or ineligible comparisons. The local application and diagnostic targets build successfully; all13 distinct focused checks now pass.

The user authorized all six remaining opportunities: oversized clipboard rejection, ProRes first frame, idle/background work, combined cache pressure, sustained batch exports and restore with unavailable media. Active editor, existing services/catalogs and unrelated dirty work are preserved. Checks are windowless. Artifacts: `perf-artifacts/performance-round13-2026-11-07/`. Root serializes builds/timings under both release/debug locks; existing Luna agents investigate independent lanes.

## Clipboard: accepted

After recipe validation and the existing deep-container legacy parser fallback, a conservative string/key byte lower bound detects guaranteed oversized JSON before the final complete byte buffer is allocated. Qt still serializes possible accepted documents exactly; no estimated bytes are emitted.

A fresh same-executable function fixture froze baseline and candidate algorithms in distinct namespaces. Baseline-only A/A controls passed for CPU/wall; all fixed B-C-C-B samples count, two processes/block and32 representative or16 oversized encodes/process. Benchmark-only User Initiated QoS was verified.

| Metric | Baseline | Candidate | Change |
|---|---:|---:|---:|
| Oversized rejection mean CPU |323.70ms|158.10ms|-51.2%|
| Oversized rejection CPU p95 |330.23ms|163.44ms|-50.5%|
| First-call peak RSS, best baseline/worst candidate |921,583,616B|711,753,728B|-22.8%|
| Representative mean CPU |12.269ms|12.571ms|+2.46%, within5% guard|

CPU/wall/p95/first-call and peak-memory guards passed. Expanded current-source contracts passed exact legacy bytes, valid decode, invalid styles/text dimensions, Unicode/escaping/lone surrogates, conservative-bound differential checks, and deep metadata containing more than 64 MiB that must retain the legacy parser fallback. The actual oversized encoder still rejects at 64 MiB. This is encoder CPU/memory evidence, not physical presentation latency. Receipts: `clipboard/measurements/comparison.json`, `clipboard/contracts.log`, `clipboard/live-limit.json`, `clipboard/decision.json`.

## Missing media: accepted

The original launched three doomed ffmpeg jobs across three offline reopens. Readability guards now preserve cached waveforms, skip failed work and drain queued missing sources with notification. The candidate launched zero doomed jobs, kept the authored edit/offline state, and generated a real 2400×160 waveform after same-path restoration. New permanent `timeline_missing_media_restore_test` passed. Timing is diagnostic.

The existing shared-cache pressure/LRU/accounting/retained-reference/concurrency contract passed. A concurrent crop-utility refactor introduced ambiguous names against private preview helpers; a private-helper rename preserves existing formulas and let the desktop runtime compile. This is build integration, not a performance claim.

## Export delivery: accepted correctness repair

A private real-JobRunner fault fixture reproduced Completed/valid integrity with no manifest. Delivery now builds/checks its manifest beside staging, refuses occupied final paths, and reports success only after publishing the pair. Cancellation/stale cleanup uses worker-captured file identity and a bounded manifest read, preserving unrelated files and avoiding GUI-side full-export hashing. Revocation invalidates its integrity metadata.

Four repeated four-photo batches passed exact output/decoded-pixel/manifest checks. Each clear removed controller/catalog rows and retained job payload while preserving delivered files. HeavyRender concurrency remained one. A late cancellation after both verified files were published removed the attempt's files and invalidated integrity; the baseline removed files but retained valid metadata. The collision directory and pre-existing final sentinel remained intact, with no partial files. These are behavior/resource-ownership checks, not a throughput or whole-process leak-free claim. The two renames are not crash-atomic, and the directory fault exercises prepublish occupancy rather than a forced failure between renames. Receipts: `exports/sidecar-baseline.json`, `sidecar-candidate.json`, `batch-baseline3.json`, `batch-candidate.json`.

After-clear RSS rose from about 99.2 to 133.3 MB in the baseline and 101.5 to 134.5 MB in the candidate across 16 exports; malloc live bytes rose about 2.9 MB in both. Rows, active attempts, pool workers and partial files returned to zero, with stable thread count. The similar baseline trend does not identify a candidate regression, and a long-run memory plateau remains unproven.

The waveform queue-disappearance regression also passed: queued source B disappeared behind a blocked A; B was skipped with readiness notification, C progressed, and B worked on later same-path restore.

## Idle/background work: measured, no production change accepted

A real Main.qml fixture warms Edit in the foreground, then measures ten seconds minimized and verifies exact selected asset/recipe state and resumed foreground stall reporting. Twelve fixed A/A and B-C-C-B runs passed their behavior contracts. Suspending frame telemetry reduced dispatcher wakeups from 118.63 to 66.72 per second, but CPU improved only 12.9% against a 20% target, and baseline wake-rate controls varied more than 5%. The candidate is unaccepted. No Main.qml sampler change is included.

An active Timer/Animation dump found no running object to attribute the remaining wakeups to. Offscreen rendering can remain virtually exposed while minimized; residual wakeups are not evidence of a specific product hotspot. A separate minimized-first startup diagnostic crashed during Edit loader incubation. Deferring prewarm avoided that diagnostic crash and preserved state, but cold Edit activation then took 1911 ms and normal foreground performance was not qualified. That candidate is also not promoted. Receipts: `idle-cache/fixed-sampler-comparison/comparison.json`, `trace-minimized/active-work.json`, `prewarm-candidate-diagnostic/prewarm.json`.

## Combined cache pressure: candidates not promoted

The existing seven-owner shared cache pressure contract passed. Library originals retain up to three full-resolution images/192MiB outside that budget. The first registration candidate released its budgeted ownership at critical pressure but increased process peak RSS by 21.25% when consumers retained images and an evicted image was decoded again. It failed the 10% memory guard and is rejected.

The second candidate uses weak reuse of consumer-retained immutable backing without a hidden strong cache. All functional and candidate latency/memory guards passed; peakRSS was only 0.10% above the best baseline. A separate correctness mode passed exact metadata, shared pixel pointers, unique/shallow-copy mutation isolation, two post-critical reuses with zero extra decode misses, and provider-destroy lifetime. However, the fixed baseline warm p95 controls varied 5.19% for wall time and 5.15% for CPU against the 5% limit. The comparison is ineligible; C2 is unaccepted and reverted without selective repeats. Both candidates, tests and receipts remain in artifacts, and the existing provider implementation is restored. This does not establish a total-process/GPU memory budget or a qualified memory saving. Receipts: `idle-cache/library-original/comparison-runs/comparison.json`, `candidate2/comparison-runs/comparison.json`, `candidate2/backing-contract.log`, `candidate2/decision.json`.

## ProRes first frame: reproduced, candidates reverted

The corrected windowless native fixture pumps the Core Foundation run loop and bounds autorelease pools per wait turn and attempt. Both codecs load, all four direct decoder controls produce frames, and production ProRes preroll intermittently times out before its first frame. The first audio-attachment candidate still missed 3 of 24 ProRes attempts and was rejected. A second candidate tested one guarded 250 ms source-start bootstrap while preserving the original two-second deadline and requested paused target; it still missed 4 of 24 attempts. Both production candidates and candidate-specific tests were reverted. The diagnostic probe remains. Stop rule: flaky results without a qualified fix; no third speculative patch. No physical display latency claim is made. Receipts: `lifecycle/prores-candidate1-bccb/comparison.json`, `prores-candidate2-bccb/comparison.json`.

The second candidate's kick executed on all four failed attempts, which still produced zero sink callbacks with media loaded/error-free. A useful next diagnostic isolates reuse: alternate the same sources on one bare QMediaPlayer with a persistent sink, then pair it with a new sink per source. Fresh-player controls already pass. That comparison can distinguish player/sink reuse from controller policy before another production patch.

## Final verification

The current-source release `photo_editor_app` built successfully, as did the diagnostic targets. Twelve of thirteen focused checks passed initially: history, clipboard, shared cache budget, playback, missing-media restore, queued-source disappearance, and six JobRunner cases. The export-history test assumed a real outage must last at least200ms; it returned its expected error sooner. Responsiveness is now checked while a controlled reader remains blocked, retaining the real-outage, read-only, stale/error/retry/mutation/destruction contracts. Its focused rerun passed. Thus all13 distinct checks pass across the final run and targeted rerun; the original failure remains in `final-ctest.log`.

Final diagnostic targets also compile without new warnings after a QByteArrayView overload cleanup in the batch pixel hasher. Existing unrelated `timeline_rig_scene.h` signedness warnings remain in the application build. The verified-delivery production source is unchanged from the successful private-catalog batch receipts. All23 recorded source hashes matched at completion, and rejected production files match their pre-experiment snapshots. The isolated PostgreSQL test clusters were removed. Receipts: `final-build.log`, `final-diagnostic-build.log`, `final-loading-ctest.log`, `final-source-manifest.json`, `rejected-candidate-restoration.json`, `final-verification.json`.

This qualifies the local dirty-tree build and focused windowless contracts. The running editor was not restarted; no physical displayed-frame, whole-process leak-free, clean-machine or release qualification is claimed.

Follow-up startup repair: a native launch exposed `CutoutResultPreview` missing from the explicit packaged QML list despite being present on disk. The registration is now added. Failed root QML loading exits before catalog bootstrap and the ready log. New `packaged_qml_startup_test` reproduces failure on the prior binary and passes on the rebuilt release app, requiring both desktop and QML route readiness plus continued process lifetime in a private windowless instance. Evidence: `perf-artifacts/packaged-qml-startup-fix-2026-11-07/`. The earlier build and source-based checks alone did not establish packaged startup.

Rejected trials follow the applied [performance-loop skill](/Users/bradyreiner/.codex/memories/skills/photo-editor-perf-loop/SKILL.md): “Revert if the target misses badly or violates a protected guardrail” and “Stop early if results are flaky, architectural risk grows, or the loop budget is exhausted.” Gates were not relaxed, and failed controls were not selectively rerun.
