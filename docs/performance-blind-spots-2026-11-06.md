# Performance blind spots — November 6, 2026

Round12 starts with three audits authorized after the existing backlog review. Existing correctness tests are useful but do not establish end-to-end resource or latency budgets for all of these workloads. Mark a fix accepted only after a matched baseline/candidate comparison, exact behavior checks and protected-workload gates.

| Area | Measurement needed | Current status |
|---|---|---|
| Cold color initialization | Cold/warm target/profile/quality changes; LUT builds, eviction, lock wait, first usable preview and total memory | C3 engine comparison passed; actual preview macro failed full-preview p95/setup controls. Cache production changes reverted; native-profile pixels remain exact. |
| AI/depth lifecycle | Source replacement, heartbeat, cancellation, teardown, stale output and child/resource cleanup | Depth-motion lifecycle repair passes expanded windowless fixture; broader real-model/GPU release proof remains open |
| History and clipboard | Real QString paint tiles and masks; undo/redo, all-zone live heap, spill and clipboard limits | Clipboard accepted: encoder CPU -43–49%, first-call peak RSS -29–35%, legacy bytes/size rejection exact. History C1–C3 rejected for CPU or uniform peak-RSS regressions; original history restored. |
| Idle resource use | CPU, timer wakeups, scene rendering and background jobs while unchanged/minimized; late work after switching workspace | Round13 actual-Main fixture added. Sampler change missed CPU target and wake controls; reverted/unaccepted. Minimized prewarm is diagnostic only; native idle/power qualification remains open. |
| Combined cache pressure | Total owned/live RAM, native/GPU memory and disk across Edit, Board, Timeline and AI, including reclamation | Round13 retained-consumer pressure fixture added. C1 failed peak-RSS guard; C2 passed behavior/candidate guards but baseline warm p95 controls varied5.19% against5%. Both restored/unaccepted. Total-process/GPU budget remains open. |
| Sustained batch output | Throughput, peak memory, encoder concurrency, cancellation, partial files and disk-full behavior over many exports | Round13 private-catalog4×4 batches pass exact pixels/manifests, cleanup, cancellation and ownership. Verified-delivery repair accepted. Memory rises similarly in both arms; long-run plateau, disk-full and crash-atomic pair publication remain open. |
| Complex typography/vector edits | Large shaped text, fallback fonts, large paths/selections, keystroke and transform p50/p95, retained caches | Tracking only; extend existing correctness cases with representative workloads |
| Session restore/offline media | Time until editable, missing-file/relink storms, disconnected media, retry/cancel and resource cleanup | Round13 missing-waveform work fixed:3 doomed launches to0 across offline restores; exact edit state, queued-source disappearance and same-path retry pass. Whole-session restore latency remains open. |

Artifacts: `perf-artifacts/performance-round12-2026-11-06/`. Root serializes all builds/tests/timings under both build locks. Agents edit only assigned fixtures or approved candidates. Default per-lane ceiling3 candidates; target latency gain10% (memory gain20% for memory-focused work), protected mean/p95 <=+5%, peak RSS <=+10%; exact outputs and lifecycle guarantees are mandatory. Lane plans freeze concrete workloads/gates before production changes.

The current active editor and existing catalogs/services are preserved. Windowless results do not establish physical displayed-frame, full native Timeline or real-model quality qualification. No journal/GPU redesign rejected in Round11 is reopened by this round.

Round12 evidence and decisions: `docs/performance-optimizations-round12-2026-11-06.md`.

Round13 continuation: `docs/performance-optimizations-round13-2026-11-07.md`. Oversized clipboard rejection improved another51.2% CPU/22.8% first-call peakRSS in its frozen function comparison. ProRes first-frame misses are reproduced in a corrected windowless native probe; both attempted production candidates failed and were reverted. A reused bare-player/persistent-versus-replaced-sink comparison is the next diagnostic.
