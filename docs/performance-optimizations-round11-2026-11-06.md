# Performance round 11 — November 6, 2026

Two production fixes were accepted: component admission compilation and bounded CPU preview color conversion. Exact GPU export and sparse checkpoint candidates were rejected and reverted when their guards failed.

Artifacts: `perf-artifacts/performance-round11-2026-11-06/`. Root owns serialized builds and measurements under both release/debug locks; Luna xhigh agents investigate scoped lanes. No active editor restart or existing catalog/service mutation. Checks are windowless and do not qualify physical displayed-frame latency.

## Accepted component compilation change

The ACES2 display-text admission probe compiled every photo layer solely to inspect color policy, then actual rendering compiled them again. The probe now builds only its color configuration, preserving its previous target/profile overrides.

Fixed B-C-C-B float-render fixture (1536×1024), unchanged exact pixel bytes:

| Workload | Baseline median | Candidate median | Change |
|---|---:|---:|---:|
| One placement | 470.432 ms | 405.611 ms | -13.8% |
| Twelve repeated placements | 2978.721 ms | 2506.117 ms | -15.9% |
| Twelve varied placements | 2949.847 ms | 2465.277 ms | -16.4% |

Protected means/p95 improved; peak RSS changed -0.6%. Display-text overlay correctness passed. This affects direct auto-admission without an already supplied canonical plan, rather than all render paths. Full evidence: `components/candidate1/comparison.json` and `final-review.md`.

## Preview contention investigation

Stage attribution identified ordinary Little CMS DeviceTransform as the dominant cost. Its existing bounded, exact parallel implementation now applies to in-memory previews up to 4,194,304 pixels (64 MiB scratch maximum), with ordinary-transform eligibility checks. Larger surfaces retain serial processing; proof remains a distinct serial stage.

An early isolated test used QCoreApplication and missed native monitor color conversion. Offscreen QGuiApplication plus identical native profile access reproduces that conversion. Mixed-permission diagnostics were excluded; all accepted B-C-C-B runs used matching profile SHA, pixels, format and extent.

| Workload | Baseline | Candidate | Change |
|---|---:|---:|---:|
| Isolated interactive CPU render | 109.33 ms | 33.22 ms | -69.6% |
| Isolated full CPU render | 182.56 ms | 55.94 ms | -69.4% |
| Under-load interactive render | 139 ms | 62.5 ms | -55.0% |
| Under-load full render | 332.5 ms | 260 ms | -21.8% |
| Under-load edit to full drain | 525 ms | 382 ms | -27.2% |

The real mixed workload imported 1024 photos while thumbnail, preview and actual atomic PNG export work overlapped. All preview/export hashes match. Complete export render/encode/publish increased 3.6%, importer 1.1%, and whole mixed drain 1.6%, within the 5% protected-workload guard. The encoding/publish subphase alone increased 5.9% (76 to 80.5 ms); this is disclosed attribution, not an export-throughput improvement. Peak RSS increased at most 4.74%, within 10%. Two process samples per binary under concurrency are limited evidence for tail latency.

The final 64 MiB/ordinary-transform admission was rebuilt and verified against the same baseline pixels; timing remains consistent. The existing serial/parallel ICC exact-float, alpha, small-image and cancellation test passed. No ACES LUT cache budget changed. Evidence: `preview/candidate1/isolated-comparison.json`, `mixed-comparison.json`, and `final-bounded-verification.json`.

## Exact export GPU candidate rejected

The candidate retained CPU tone processing and ran only PhotographicOutput on Metal. Runtime stage maps confirmed that route. It failed exact final pixel parity in both primary SDR8 formats at +0.75 EV and all sampled -2, -1, 0, +1.5, +2 EV rows. Auto and file output matched each other but differed from CPU. Protected 16-bit Display P3 stayed exact on CPU.

All three production files were restored from lane snapshots, preserving the accepted component change and diagnostic telemetry. The exact CPU fallback remains. Existing Metal LUT tests allow small channel/Delta-E errors and do not establish byte-identical export. No tested exposure subset qualified; no tolerance was relaxed. Evidence: `gpu/candidate1/parity.json`, `RESULT.md`, `reverted-manifest.json`.

## Checkpoint candidates rejected at the three-attempt ceiling

The final typed-record candidate passed native/portable hashing and mapped/scanner recovery suites, including exact state/pixels, failure retention, malformed records, torn tails, numeric Qt subtype normalization, and the exact 16 MiB line boundary. Independent source review found no remaining concrete mapping-lifetime or data-loss defect.

Its fixed B-C-C-B comparison used two processes per block (four processes per binary), identical workloads and a fresh original-journal baseline rebuilt with the same final harness. The harness added recovery timing, normal-save raw samples/means and process RSS without changing write timing boundaries or sample counts.

| Final candidate metric | Baseline | Candidate | Change |
|---|---:|---:|---:|
| Sparse paint write p50 | 247.797 ms | 5.369 ms | -97.83% |
| Sparse paint write p95 | 331.897 ms | 5.546 ms | -98.33% |
| Sparse recovery | 1088.560 ms | 1197.394 ms | +10.00% |
| Small-tone recovery | 0.540 ms | 0.590 ms | +9.27% |
| 1 MiB tone write p95 | 197.166 us | 213.584 us | +8.33% |
| Worst dense-save RSS vs best baseline | 3,079,159,808 B | 3,388,555,264 B | +10.05% |

The recovery/normal-save 5% and memory 10% guards failed. The first candidate had also regressed large-mask save work; the second recovered roughly twice as slowly. No fourth attempt or adaptive repeats were made. All journal production changes were reverted exactly from frozen snapshots, the new typed helper was removed, and the original journal test was restored. Diagnostic benchmark additions remain useful with the original implementation. No new journal format ships in the resulting app.

Evidence: `checkpoint/candidate3/comparison.json`, `RESULT.md`, `reverted-manifest.json`, and the candidate1/2 results. The candidate sources and tests remain artifact-only for future investigation.

## Final verification

The release `photo_editor_app` build passed. Seven focused CTests passed with zero failures/skips: native and portable journal recovery, save queue, serial/parallel ICC, display-text overlays, CPU preview and export parity. The final original-journal mixed paint/exposure/transform benchmark also reopened exact state and decoded pixels. The final preview matched all accepted-baseline pixel hashes and the same monitor profile.

Final source/app hashes were unchanged during verification; rejected journal and GPU files match their frozen originals. Narrow accepted-source diffs produced no whitespace diagnostics. The app build retains an existing signedness warning in `timeline_rig_scene.h`; that file was not changed here.

Evidence: `verification/final-status.json`, `final-source-manifest.json`, `final-app-build.log`, `final-focused-ctest.log`, `final-preview-cpu.json`, `final-export-exposure.json`, and `final-original-journal-mixed.json`. The active editor has not been closed or restarted. These windowless results do not qualify native visible Timeline memory behavior or physical displayed-frame latency.
