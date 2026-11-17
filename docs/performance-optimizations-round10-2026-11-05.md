# Performance round 10: broader workloads

This round uses Luna xhigh agents to investigate the seven broader workload gaps.
The open editor and unrelated dirty-tree work were preserved. Measurements and raw
logs live in `perf-artifacts/performance-round10-2026-11-05/`. Work began November 5
and verification continued November 6. No commits or service changes were made.

## Retained fixes

### Partial texture upload preparation

Partial Metal uploads now crop before converting straight ARGB32 and straight
half-float images to the corresponding premultiplied format. Compatible formats
remain shared. Cross-format conversions preserve full-image alignment because
Qt rounding otherwise changed some pixels.

For a 128×96 region of a 3072×2048 image, median CPU preparation fell from
**0.296125 to 0.0088335 ms (97.0%)** for SDR and **0.762125 to 0.019083 ms
(97.5%)** for HDR. Protected whole-frame/direct paths passed the +5% mean/p95
limits; peak RSS increased 0.042%. This measures conversion work, not display latency.
Exact CPU bytes, clipping, alpha, odd origins/strides, profiles, DPR2 and signed/HDR
values passed. Actual Metal texture readback parity also passed on the M4 Pro.

Evidence: `display/attempt2/`, `display/metal-native.log`, `display/hardware.json`.
Physical HDR monitors and migration between displays remain unqualified.

### Layer blend dispatch

The renderer resolves each layer's blend mode once into a private operation enum.
Pixel loops use that operation directly, avoiding repeated string checks. Normal
blending also skips channel calculations used by other modes. The blend formulas,
name aliases and unknown-mode fallback are preserved.

Twelve-placement render median fell from **6364.86 to 3006.04 ms (52.8%)**.
One placement improved 51.8%; varied transforms/opacity improved 50.5%.
All repeated floating-point hashes match the baseline. Multiply, Screen and actual
Soft Light mean/p95 passed protected limits. Peak RSS increased **6.54%**, within
the declared 10% cap; this is a latency improvement, not a memory reduction.

Evidence: `documents/compositor-attempt2/comparison.json`, raw B-C-C-B receipts,
and `corrected-soft-light/`. The original Soft Light fixture spelling was invalid;
the corrected supplemental B-C-C-B comparison rebuilt both the frozen original
engine and unchanged candidate, without rerunning the accepted macro receipts
or loosening limits. Candidate 1 remains rejected.

### Timeline stale frame rejection

The native sink snapshots frame-work generation when a decoder signal arrives.
Delivery on the GUI thread rejects callbacks invalidated by a seek, source change,
output detach or suspension. Cached replay on output attach/reactivation also
requires a current video Program clip and valid source context. This prevents
obsolete work and an old cached frame appearing with an empty document.

Regression cases emit the actual sink signal on a worker thread, exercise
stale-before-seek/fresh-after-seek delivery, reject empty-document replay, and
preserve valid late attachment and same-position recovery. Focused scheduling
and playback tests pass. Native sustained workload qualification is described below.

### Import worker shutdown

The import-poll pool is owned by QCoreApplication and drains before Qt teardown.
This repairs an observed exit crash in Qt socket initialization from a static
pool worker. Model destruction remains nonblocking; concurrency remains one.

The subprocess regression holds a queued poll, destroys its model, releases the
poll through a real localhost preflight/helper, and requires a witness constructed
before the application to run only after the application destructor returns.
The strong witness passed: `storage/poll-shutdown-strong-witness.log`.

## Broader workload coverage

| Workload | Evidence and result | Qualification |
|---|---|---|
| Import + thumbnails + edit + export | A private PostgreSQL cluster committed 1,024 byte-unique JPEGs through CatalogAssetModel, JobRunner and import_catalog. Every copied hash and committed row matched. 38 thumbnail completions; real import/render/export overlap; rendered/exported pixels exact. | Generated JPEG corpus on local storage; recipe persistence disabled in this render test. |
| Interactive edit under that load | Render telemetry: first feedback 142 ms (141 ms render, 1 ms queue), full restoration 478 ms. Final image-sink fixture observation 528 ms. Separate export render 288 ms and encode/atomic publication 76 ms. | Headless CPU image sink; one diagnostic baseline, no claimed scheduling speedup or physical display latency. |
| Rapid image switching | 98 requests, 95 replacements, pending high-water one, two cancellations, no stale publication, exact final image pixels. Authored DNG decoder/pyramid coverage passed. | Real JPEG decoding and synthetic DNG format coverage; no genuine camera RAW corpus was available. |
| Large nested documents | Whole renderer, 1536×1024, one/twelve placements plus varied transforms/opacity, floating-point output hashes and existing workspace/history/reopen/export workflow. | Once-per-layer blend dispatch reduced twelve-placement median 6364.86→3006.04 ms (52.8%), with exact floating-point output. Corrected Soft Light guard passed; all declared gates passed. |
| Storage/preflight | 10k/100k local-file scan 334.8/3933.6 ms; existing warm policy reuse 21.3/204.2 ms with exact candidates and zero rescan. Latest-source cancellation 4.7 ms. | Empty file fixtures; OS caches unspecified; no device-cold/external-storage claim. |
| Sustained native Timeline | Generated H.264/AAC, HEVC/AAC and ProRes/PCM, 96 clips, proxy/original switching, effects, playback, eight seeks and close/reopen over twelve cycles. Two complete twelve-cycle runs passed; an intervening diagnostic failed a paused original-ProRes seek. | Exposed Cocoa window was inactive. Native decoder/audio diagnostic, not physical presented-frame proof; failure remains under investigation. |

Real-import evidence: `scheduler/real-import-private-pg.json` and `.log`. The wrapper
creates and stops only its disposable `/private/tmp/looksy-round10-import-*` cluster,
with a Unix socket and no TCP listener. Existing PostgreSQL and editor sessions
were not changed. The initial mixed fixture measured preflight rather than committed
ingestion; its corrected production export evidence is separately retained in
`scheduler/production-export-baseline.json`.

Timeline evidence: `lifecycle-timeline/baseline-targeted.jsonl` and
`paired-deferred-drain.jsonl`. The complete run's processed-frame cache settled at
132,710,400 bytes/36 entries, while process RSS rose from 228.5 to 403.6 MiB.
Manual event pumping does not normally drain DeferredDelete; paired explicit drain
sampling was added to distinguish fixture retention. The first paired run failed at cycle 1. A second diagnostic completed twelve cycles
with all codec/route/audio checks, and paired RSS measurements were identical before
and after draining DeferredDelete: 230.7→403.5 MiB. Therefore the earlier growth
was not explained by these queued deletions. The cache plateau does not account
for all RSS growth; native decoder/allocator retention requires further attribution.
Neither a production leak nor a memory fix is established. The intermittent no-first-frame
ProRes timeout remains disclosed; it occurs upstream of target seek issuance.
Evidence: `lifecycle-timeline/paired-deferred-drain-diagnostic2.jsonl`.

## Rejected experiments

The [photo-editor-perf-loop skill](/Users/bradyreiner/.codex/memories/skills/photo-editor-perf-loop/SKILL.md)
requires: “Revert if the target misses badly or violates a protected guardrail.”
The declared protocol uses B-C-C-B comparisons, a 10% target gain, protected
mean/p95 regression limits of 5%, peak RSS limit of 10%, exact output, and at
most three distinct candidates per target. Failed candidates were not resampled
to obtain favorable results and limits were not loosened.

- **Import-details cache:** all three candidates improved repeated warm requests
  around 90%, but each failed a protected cold/revision limit (5.09%, 5.46%,
  6.37%). The cache and related worker API changes were fully reverted. Only the
  independent shutdown fix remains. Evidence: `storage/details-*.json`,
  `storage/attempt2/`, `storage/attempt3/rejected-source/`.
- **Shared-container packing:** packing improved around 68%, but protected
  inflation failed. Both candidates were rejected; the production object-table
  header matches its initial snapshot. Meaningful type-strict tests and opt-in
  workload remain. Evidence: `documents/` and `documents/attempt2/`.
- **Compositor candidate 1:** a normal-mode early return reduced twelve-placement
  median render from 6312.85 to 3017.78 ms (52.2%) with exact floating-point hashes.
  Multiply latency and peak RSS (+13.29%) failed protected limits. The original
  soft-light fixture used unsupported `soft_light` spelling; its results actually
  measured unknown-mode fallback and are invalid for Soft Light. Candidate 1 is
  rejected regardless of that fixture mistake. Candidate 2 resolves blend modes
  once per layer. Its unchanged full-render/multiply/screen comparisons and RSS
  (+6.54%) passed. The corrected real Soft Light guard also passed (mean −2.26%,
  p95 −4.09%, exact hashes), so candidate 2 is accepted.
  The correction changes the fixture, not the production candidate or acceptance limits.

## Final verification

The current Looksy app and 22 focused build targets compiled successfully. All
**19 final focused regression tests passed**, including layer alpha/effects/styles,
RAW/HDR component workflow, component history/reopen/export, catalog/preflight,
thumbnail/decode queues, and Timeline scheduling/playback/cache. The separate
strong shutdown witness passed after its timeout cleanup improvement. Actual
Metal parity and the real committed-import integration passed separately.

Evidence: `verification/final-build.log`, `final-focused-tests.log`,
`final-source-before.json`, `final-source-after.json`, `final-binaries.json`, and
`final-diff-check.log`; the 21 recorded source hashes stayed stable through the
final build and verification. Binary hashes include Looksy, the relevant fixtures,
both frozen render engines, and catalog helpers. Scoped whitespace checks passed.
The private PostgreSQL cluster stopped and was removed; zero private cluster
directories remained. No editor session was closed or reopened.

Remaining investigation is native first-frame/preroll latency, the intermittent
original-ProRes no-first-frame timeout, and native process RSS growth after the
bounded frame cache has warmed. There is no accepted patch for those observations.
Genuine camera RAW, external/slow-device, physical HDR/multidisplay and active
foreground presented-frame qualification require representative inputs/hardware.
The cache experiments exhausted their bounded attempts and were reverted. No
hardware or genuine camera RAW qualification is inferred from synthetic tests.

## Memory follow-up, November 6

The subsequent [long-session investigation](timeline-session-memory-2026-11-06.md)
found a polling-harness native autorelease retention confound. A fixed-binary
B-C-C-B isolated native-player comparison reproduced it and removed 97.1% of
post-warm live-heap growth with a correct cycle lifetime boundary. The corrected
windowless regression passed 128 decode/teardown cycles and its negative control
failed the memory guard as intended. The full 96-clip Timeline fixture now has the
same lifetime correction and samples after document destruction, but its complete
visible-window rerun remains unqualified. The earlier RSS figures should not be
treated as a proven production leak. No app cache budget or product code changed.
