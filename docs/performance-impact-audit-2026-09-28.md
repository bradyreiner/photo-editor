# Looksy performance impact audit — 2026-09-28

30 ranked findings and optimization opportunities across Library, Import, photo editing, rendering, Carousel, Timeline, export, persistence, and visual search.

The original audit reviewed the working tree at base commit `57609cb`, including existing uncommitted changes. It combined source inspection with existing benchmark artifacts and did not change application code or run new builds, tests, or benchmarks. Subsequent implementation work has changed several paths; see the status update below. Historical timings are evidence of earlier behavior, not a fresh baseline of this working tree.

## Current source status

The [October 5 interaction follow-up](interactive-stall-performance-2026-10-05.md) measures video insertion, adjustment deletion, and brush feedback in the native shell. It retains reduced insertion publications and deferred paint-history formatting, records exact export/undo parity, and keeps the failing warm-brush budget and recipe-preparation stalls explicit.

The ranks below preserve the original impact ordering. They are not all still unimplemented:

- **1 — Duration probes:** now dispatched to a bounded worker pool, with revision-aware caching and incremental publication. Pending probes follow the current selection and viewport through an indexed queue. Isolated controller tests passed three times for scheduling, cache reuse, invalidation, stale results, and timeouts, including a generated WAV through real ffprobe. Active-shutdown checks now pass in three runs. A native 1,000-row synthetic Library passes publication, held-probe scrolling, exact duration counts, and zero-probe warm refresh. Full native responsiveness remains open: hidden Edit prewarm produces a 766 ms median maximum heartbeat gap; three bounded candidates failed the <=100 ms target and were reverted. See [native evidence](evidence/performance-2026-09-28/library-duration-native-prewarm.json).
- **2 — Project membership:** now uses asynchronous batched persistence and a durable membership outbox. Three isolated controller runs passed for 1,000-asset batching, responsiveness during held persistence, latest intent, stale snapshots, failure/retry, restart replay, and corrupt recovery preservation. Verification found and fixed a project-name capitalization mismatch. Three fresh temporary PostgreSQL clusters now pass real-helper checks for one-transaction 1,000-asset writes, retries, removals, missing IDs, rollback, deletion, and bound project names. Active-save shutdown now cancels its owned helper while preserving replay intent. Three integrated warm native macOS runs pass with 1,000 cached thumbnails and exact real-database readback: commands 20–21 ms, one reset/helper, maximum scroll-to-frame 11.64 ms. Packaged/cold/other-platform behavior remains unverified. [Evidence](evidence/performance-2026-09-28/project-membership-native-shutdown.json).
- **3 — Carousel export:** app actions now render and encode through an independent worker renderer. Runtime acceptance remains pending; synchronous preflight remains a separate issue.
- **4 — Preview readback:** pixel-inspector sampling, reduced scope sampling, asynchronous diagnostic overlays, and final-display copies for comparison/paint bases are present in the working tree. Eligible paths can read native BGRA8 images without forcing float CPU preview materialization; regional paint checks matching base/patch output paths and profiles before composition. A native 1800x1200 exposure-edit proof now shows settled CPU publication replacing the interactive Metal frame, so the retained one-pixel inspector path is not reached. Three bounded candidates failed and were reverted; color parity and transfer reduction remain unverified. [Evidence](evidence/performance-2026-09-28/native-inspector-settled-blocker.json). Rendering-upgrade evidence and exact soft-proof diagnostics retain their CPU requirements. A later scoped ICC identity repair shares equivalent prewarm/CPU/canonical tables, eliminating nine duplicate rebuilds in a repeated nine-request fixture. Exact table and processor parity, three runtime suites, host Metal ICC parity, and app build pass; settled native publication remains open. [ICC identity evidence](evidence/performance-2026-09-28/device-lut-identity.json). A later direct-worker candidate now reaches settled native output, but failed CPU/ICC pixel parity. Two stricter interpolation diagnostics passed the image fixture and then failed the broader seeded color corpus; all source changes were reverted. ICC cell accuracy is the remaining prerequisite before another native admission attempt. [Worker diagnosis](evidence/performance-2026-09-28/settled-native-worker.json). Shared exact ICC application now removes a redundant per-handle mutex: matched four-call batches are 73.7–74.3% faster with exact bytes, stable serial latency, bounded RSS, six passing checks and a desktop build. This is engine throughput evidence, not native tool latency. A separate denser ICC validation prototype passes over one million samples but is not integrated. [Concurrency evidence](evidence/performance-2026-09-28/icc-concurrency.json); [accuracy prototype](evidence/performance-2026-09-28/icc-cell-coverage.json). The subsequent three-candidate integration passed sampled color accuracy and cold-build limits but failed its peak-memory gate (183.1 MB versus a 180.8 MB cap for the best candidate); all integration changes were reverted. [Integration evidence](evidence/performance-2026-09-28/settled-integration.json).
- **5 — CPU effects:** shared workers and tile/mask scratch reuse are implemented. Premultiplication once per source pixel and interleaved convolution pass matched CPU gates: tiled diffusion 426.573 → 228.544 ms at 2048 × 1366, exact hashes across 63 cases, and bounded RSS. Spatial/Gaussian/posterize/codec contracts pass three times. Real-photo CPU previews and 16-bit exports now match. A provisional streamed adjustment-layer output removes a full-frame allocation and completes 39.7 MP preview/export plus a synthetic 45 MP exact-pixel contract. Six focused suites and the desktop build pass. Repeated native timing acceptance stopped under unstable host timings; GPU effects, content-layer allocation limits, and packaged visual acceptance remain open. [Kernel evidence](evidence/performance-2026-09-28/cpu-effect-kernels.json); [streaming status](evidence/performance-2026-09-28/effect-streamed-output-event-loop.json). Supplemental matched engine runs now show 366 MiB lower peak memory at 24 MP diffusion and 686 MiB lower at 45 MP blur, with matching hashes, passing latency guards, and default-budget completion where the old 45 MP path rejects. Foreground acceptance remains open. [Engine evidence](evidence/performance-2026-09-28/effect-stream-engine.json). Content effects now reuse their private raster through bounded delayed rows: 24 MP Gaussian/diffusion save 343–358 MiB with matching output and passing latency guards; 45 MP Extend-edge blur completes at the original scratch limit. Transparent expansion still has its separate raster cap. Five suites and the desktop build pass; native visual acceptance remains open. [Content evidence](evidence/performance-2026-09-28/content-effect-rows.json). Parallel content rows now reduce matched 24 MP plain/rotated CPU composition by 82–84%, Gaussian content workflow time by 60%, and diffusion by 24%, with identical output, bounded RSS, and passing small-layer/nested-worker guards. Foreground verification remains pending. [Compositing evidence](evidence/performance-2026-09-28/content-compositing.json). The layer stage also skips its unused mask-edge image snapshot, saving another 365–367 MiB in three matched 24 MP workloads; 40 output comparisons, mask activity checks, seven suites, and the desktop build pass. [Mask-preparation evidence](evidence/performance-2026-09-28/unused-mask-source.json). Fully clipped final transparent padding is now skipped for eligible untransformed content: matched Gaussian runs save 369 MiB at 24 MP and 697 MiB at 45 MP, with identical output and passing timing guards. The larger render completes at unchanged production limits; transformed and partial content retain expansion. [Padding evidence](evidence/performance-2026-09-28/clipped-effect-padding.json). A separate accepted CPU-compositor change skips the unused luminance plane when Texture/Clarity are off: 24 MP diffusion uses 90.6 MiB less peak memory in three matched pairs, with all 22 output-hash contracts unchanged and latency guards passing. [Memory evidence](evidence/performance-2026-09-28/effect-detail-plane.json). Hidden comparison/loupe and inactive viewport image sources now load on visibility: three native pairs reduce edited-image requests from 60 to 12, with exact preview/export/native-readback pixels and 24 nonblank captures. Median gates pass; multi-second outliers, intermittent blank presentation, and larger native workloads remain open. [Native request evidence](evidence/performance-2026-09-28/hidden-tool-images.json). A standalone Metal Gaussian prototype failed pixel-accuracy and small-blur timing gates; a separate CPU interior-loop candidate preserved pixels but regressed timing and was reverted. No additional product change was retained. [GPU experiment](evidence/performance-2026-09-28/gaussian-metal.json); [CPU experiment](evidence/performance-2026-09-28/gaussian-interior.json). A subsequent retained-tile convolution change passes exact-output and memory gates: targeted tiled Gaussian/diffusion time falls 20–22% at 1024/2048 widths, and actual-worker warm Optical Diffusion falls 166 to 120 ms. All 64 worker images (48 synthetic, 16 portrait) match exactly, 84 tile contracts and four suites pass, and the desktop builds. Foreground/native-frame acceptance remains open. [Retained-crop evidence](evidence/performance-2026-09-28/effect-retained-crop.json). Retained regions now propagate through Gaussian/Unsharp/diffusion stacks: matched kernel work falls 32–37%, and warm two-effect worker renders fall from 369/365 to 240/238 ms. Preview and full-resolution 16-bit export pixels match exactly; an extreme-HDR scratch regression is covered. Export timings were unstable and are not used as a speedup claim. [Multi-effect evidence](evidence/performance-2026-09-28/effect-stack-retained-regions.json).
- **6 — Catalog snapshots:** worker parsing and metadata preparation, indexed facets/projects, and streamed helper output are implemented. Bounded parallel recipe projection now passes matched controller gates: 10k assets ready in 2,934 → 1,620 ms, unchanged exact IDs/recipes, bounded heartbeat/RSS, and passing cancellation/recovery/thumbnail contracts. Whole-library recipe transfer, desktop buffering, paging/incremental publication, and real-database/native acceptance remain open. [Evidence](evidence/performance-2026-09-28/catalog-snapshot-preparation.json).
- **7 — Recovery checkpoints:** unchanged active/rejected/migration-retry records can reuse exact serialized JSON; envelope output avoids another full-payload copy. An isolated rejected-record benchmark improved p95 by 37.9%, with 8.3% higher peak RSS; current/portable recovery suites passed. Pending-only checkpoints use a direct serialization path to limit regressions. A further journal-owned cache now reuses exact unchanged mask encodings: three matched pairs reduce 4 MiB tone checkpoint median p95 from 32.28 to 20.19 ms and native Cocoa/Metal gesture commit from 51.47 to 39.54 ms, with exact recovery and bounded RSS. Changed masks, full payload hashing, and synchronous file rewriting remain; packaged/real-photo and database-helper-inclusive acceptance is pending. [Evidence](evidence/performance-2026-09-28/checkpoint-mask-encoding.json). A changed-mask payload-sharing candidate was rejected and reverted: its 16 MiB speedup missed the gate and RSS rose about 154 MiB. Expanded checkpoint/representation fixtures remain. [Rejected attempt](evidence/performance-2026-09-28/changed-mask-encoding.json). A separate compact-JSON candidate was also removed after final 4 MiB brush timing missed its 20% target; the new serializer timing probe remains. [Serialization attempt](evidence/performance-2026-09-28/checkpoint-json.json).
- **8 — Cold workspace loading:** Carousel's 31 dialog contents now load from separate QML files on first opening and retain their controls afterward. An identical source-QML route comparison measured cold Carousel loading at 2,443 → 1,182 ms (51.6% lower), with peak process RSS 9.4% lower. All 31 dialog lifecycle checks passed. Compiled production/native timing and remaining Carousel/Timeline readiness costs are still open.
- **9 — Timeline frame starvation:** ordinary frame arrivals now retain the active worker and replace only the newest pending frame. Actual seeks, source/grade changes, output changes, recovery, and destruction still invalidate work. A deterministic overload fixture reproduced the previous failure and passed three times after the change, including stale-output rejection and reentrant publication. Real decoder/GPU playback and continuously animated grades remain acceptance work.
- **10 — Carousel preview generation:** board rendering, adjusted-item decoding, PNG encoding/writes, and cache cleanup now run on workers. A bounded snapshot queue coalesces dirty boards/items and rejects stale results. Three isolated runtime runs passed for pixel parity, responsiveness, cache reuse, late results, document replacement, and destruction; the Carousel UI shard also passed. Large-document native timing and further disk/publication optimization remain open.
- **11 — Timeline CPU conversion:** mapped-plane pointers/strides are now resolved once per frame. Two reversed-order comparisons retained exact output across 66 fixtures and reduced conversion p95 by 26.1–63.1% across five large cases (49.0% geometric mean), with essentially unchanged peak RSS. Native decoder/GPU and playback-cadence acceptance remain open.
- **12 — CPU concurrency:** large RGBA16/RGBA32 packing now shares persistent color/effect workers instead of creating threads per image. Parallel batches stay within that worker set and nested work stays inline. Other pools, priority scheduling, and runtime acceptance remain open.
- **13 — Import preflight:** review and confirmation scans now run on a bounded worker with progress, cancellation, stale-result rejection, and short-lived reuse of source facts across policy changes. Confirmation always rescans. Three isolated controller runs and a focused real-QML Import check passed. Large native workloads and remaining manifest/job-store admission I/O remain open.
- **14 — Export preflight:** UI bindings now read cached worker reports; source checks are reused across scope/destination changes with revision checks and a bounded cache. Batch submission validates fresh inputs on a worker and rechecks filenames at admission. Three controller runs and a focused real-QML Export check passed, including close protection during submission. Native large-RAW, memory, real catalog, and full export acceptance remain open.
- **15 — Photo cache budgets:** seven retained caches now share a RAM-based ceiling and eviction order, with pressure trimming and per-owner diagnostics. The focused cache/controller test passed three runs. In-flight allocations, other caches, native peak RSS, hit-rate effects, and runtime acceptance remain open.
- **16 — First brush feedback:** first-use and stroke-setup timing are now mandatory gates, with a painted-Canvas/frame receipt and focused native route. Final isolated native runs measured 87 ms first feedback and 95 ms with auto-mask; both fail the gate. Auto preview preference fell back to CPU for layer stages. Two earlier scheduling candidates missed the gate and were reverted. No latency fix is claimed.
- **17 — Catalog thumbnails:** snapshot workers validate existing thumbnail provenance; unchanged source/recipe/cache dependencies retain the published URL. Three focused runs passed: ten unchanged snapshots start zero workers, while targeted edits/replacements invalidate one asset. Native large-library and live-database acceptance remain open.
- **18 — Timeline catalog reconciliation:** ID-indexed updates, unchanged-record detection, selective metadata work, and batched row notifications are implemented. With 10k photos/1k Timelines, isolated refresh p95 fell 56.77 → 0.54 ms and edit p95 50.55 → 1.16 ms; peak RSS rose 0.33%. Three focused contracts passed three times. Native Library frame latency remains unmeasured.
- **19 — Library filter resets:** measured; three incremental-publication/storage candidates preserved row identity but regressed 100k alternating filtering (best narrowing 50.59 ms versus 17.35 ms baseline). All were reverted. The standalone GridView evidence does not cover the current Studio gallery-row cache; the issue remains unresolved.
- **20 — Photo-history memory:** a content-addressed payload store and exact state-record adapter are implemented and tested in isolation. The 256 MiB payload fixture retains 32 MiB in the store; 100 large-mask revisions restore exactly. Revision handles and asynchronous reads are now wired into HistoryService and the controller, but the first integrated RSS measurements regressed; a watcher-lifetime follow-up is awaiting remeasurement. Named-snapshot persistence remains unchanged. This item is partial; the user requested moving on.
- **21 — Metal allocation/locking:** cache locking now ends before per-render allocation, upload, and encoding. Three direct-Metal comparisons reduced median preview submission p95 10.072 → 7.625 ms (24.3%) with exact output; concurrent peak RSS rose 3.1%. Three cache-eviction contracts passed. Scratch pooling and native desktop acceptance remain open.
- **22 — Timeline undo:** unchanged immutable snapshot clip arrays are shared by value; changed clips remain independent from live pointers. Final 10k-clip/ten-track comparisons reduced capture p95 2.272 → 1.335 ms and peak RSS 287.91 → 130.89 MiB. Five focused restoration tests passed three times. Byte budgets, operation-level undo, and native acceptance remain open.
- **23 — Carousel history/autosave:** immutable capture and bounded worker autosave are retained. Exact input comparisons now reduce 200-board dirty checks from 115–121 ms to 0.7–2.3 ms and transactions from 233–235 ms to 1.4–2.8 ms, with exact output and field/lifecycle contracts. Full undo restoration, broader memory/lifecycle workloads, and native acceptance remain open.
- **24 — Retained workspace pickers:** hidden refreshes are gated, visible bursts coalesced, and picker rows contain only nine required fields. In matched 10k-asset Qt fixtures, Carousel opening falls 234.925 → 23.606 ms and Timeline 1562.960 → 55.073 ms, with exact picker fields/order and lower peak RSS. Field/filter/selection/lifecycle contracts pass. Incremental publication, closed-list retention, and native workflow acceptance remain open.
- **25 — Diagnostic logging:** a bounded writer batches file/console output and reuses prepared event fields. In matched compact-mode fixtures, producer p95 falls 180.375 → 14.333 µs with one producer and 524.833 → 363.083 µs with four. The 40k-event drain falls 5900.195 → 352.676 ms; peak RSS rises 13.656 → 21.500 MiB. All ten final workload groups pass latency/memory/content gates, and delivery/lifecycle contracts pass. Native input/render timing, interactive/slow sinks, and native teardown remain open.
- **26 — Verified import copies:** operation-local source checksums are reused across backup/managed copies with source-version checks and independent destination hashes. In matched four × 16 MiB fixtures, import p95 falls 514.458 → 403.074 ms (21.7%); source checksum reads fall 128 → 64 MiB. All twelve workload groups and 24 importer contracts pass. Coarse/unsupported fingerprints retain per-copy source hashing. Real card I/O, native responsiveness, large-import memory, and other operating systems remain open.
- **27 — Job-class scheduling:** per-attempt ownership and bounded heavy/light admission are implemented, with conservative path, memory, foreground, and import-worker guards. Six policy/controller targets passed three times. Final synthetic copy completion p95 improved 1239 → 268 ms, but the heartbeat guard missed (76 → 89 ms) in a comparison overlapped by a repository search. Final/native performance acceptance remains open; moving to 28.
- **28 — Bulk culling:** rating/flag writes now coalesce into batches of at most 256, with durable pending recovery, exact receipt handling, newer-edit protection, and missing-asset reporting. Matched 1,000-rating Qt fixtures reduced helper launches 1,000 → 4 and median drain 52,875 → 236 ms; single-photo completion rose 45 → 77 ms. Controller/recovery contracts passed three times, and isolated real PostgreSQL transaction/helper checks passed. Native throughput, very large outbox checkpoint costs, and abrupt power-loss acceptance remain open.
- **29 — Timeline cache diagnostics:** reports and cleanup run on one background worker, with a bounded saved-document index and fresh deletion reference checks. Matched controller getter pairs fell 342.691 → 0.381 ms; critical-pressure calls fell 174.773 → 0.035 ms, with exact counts and zero unchanged-document reads on warm scans. Recovery contracts passed three times. Native and full regression acceptance remain open.
- **30 — Visual-search preparation:** persistent revision aliases skip decode/encode on saved-description hits. Matched 1,000-preview controller scans fell 1339.798 → 264.969 ms warm and 1348.108 → 260.648 ms after controller replacement, with exact results and zero warm preparations. Cold indexing rose 17.0% within its declared guard; peak RSS was unchanged. Revision, migration, retention, forgetting, and existing protocol contracts passed three times. Native/real-model/other-platform acceptance remains open.

See [implementation and acceptance status](performance-fix-progress-2026-09-28.md) for detailed boundaries. The original status refresh inspected source only; subsequent implementation and measurements are recorded in the progress document. Function names are the durable evidence references below; numeric line anchors reflect the original audit and can shift as implementation proceeds.

Ranking weighs visible freezes, frequency in normal workflows, affected workload size, and evidence strength. It is an engineering priority order, not a measured speedup estimate. Lower entries are valuable mainly at scale or in specific workflows. Savings overlap and must not be added together.

Evidence labels:

- **Code:** the expensive operation and its caller are present in current source; its current wall time is unmeasured.
- **Historical + code:** earlier measurements and a relevant current code path exist; repeat the measurement before accepting a fix.
- **Recheck:** an earlier measured symptom needs current profiling because implementation has changed.
- **Conditional:** the mechanism is present, but its impact depends strongly on concurrency, project size, hardware, or feature usage.

Effort: **S** = localized change; **M** = a subsystem change; **L** = architecture or rendering work requiring broader correctness proof.

## Ranked list

| Rank | Finding | Expected impact | Evidence | Effort |
| ---: | --- | --- | --- | :---: |
| 1 | Catalog loading probes every video/audio file synchronously on the UI thread | Whole-app freezes in mixed-media libraries | Code | M |
| 2 | Adding a selection to a project runs one blocking database helper per asset | Long freezes during routine Library organization | Code | M |
| 3 | Carousel export performs rendering, encoding, verification, and video waits on the UI thread | Frozen editing and delayed cancellation during export | Code | M–L |
| 4 | Photo preview publication can require full GPU-to-CPU readback | Repeated slider, paint-layer, comparison, and diagnostic latency | Historical + code | L |
| 5 | Photo effects use expensive CPU convolution; some desktop layer routes reject GPU execution | Slow effects and exports, especially at high resolution | Code | L |
| 6 | Catalog refresh fetches all assets and recipes, then parses/projects everything on the UI thread | Startup/refresh time and memory grow with the entire library | Code | L |
| 7 | Every accepted edit synchronously rewrites the entire recipe recovery checkpoint | Stutters with large masks, paint data, or pending saves | Historical + code | M–L |
| 8 | First-use workspace construction remains a major readiness cost | Seconds before Carousel/Timeline become usable | Recheck | M–L |
| 9 | Incoming video frames invalidate still-running frame processing | Playback can stop presenting frames when processing falls behind | Code | M |
| 10 | Carousel preview refresh decodes, paints, and writes PNGs on the UI thread | Editing hitches that grow with boards and edited images | Code | M–L |
| 11 | Timeline video color processing maps frames to CPU images and makes multiple pixel passes | Dropped frames and expensive 4K/high-frame-rate playback | Code | L |
| 12 | Multiple thread pools and per-image thread creation compete without one CPU budget | App-wide latency spikes under concurrent work | Conditional | M–L |
| 13 | Import preflight walks folders and stats files synchronously | Freezes when importing large cards or slow storage | Code | M |
| 14 | Export preflight performs file/profile/RAW inspection through synchronous QML calls | Slow export controls and large-selection stalls | Code | M |
| 15 | Photo caches have independent fixed budgets outside Timeline's memory-pressure response | Memory pressure, eviction churn, and repeated decoding | Conditional | L |
| 16 | First mask-brush feedback has a historical latency spike excluded from the ordinary brush budget | Noticeable delay on the first stroke | Recheck | S–M |
| 17 | Refresh clears valid thumbnails even when their source and recipe are unchanged | Repeated thumbnail scheduling, cache reads, and publication | Code | M |
| 18 | Timeline-to-catalog reconciliation searches/rebuilds whole collections repeatedly | Library stalls as catalog and timeline counts grow | Code | M |
| 19 | Library filtering copies rows and resets the entire model | Search/filter/culling jank in large libraries | Code | M |
| 20 | Photo history is count-limited rather than byte-limited; named snapshots rewrite a global file | Memory growth and snapshot-save stalls with raster-heavy edits | Conditional | M–L |
| 21 | Metal renders allocate large scratch buffers while holding a shared cache mutex | Allocation overhead and contention during overlapping renders | Conditional | M–L |
| 22 | Timeline undo captures every track and clip for a local edit | Edit latency and retained memory grow with project size | Code | L |
| 23 | Carousel history and autosave serialize whole documents on the UI thread | Periodic pauses and growing history memory | Code | M–L |
| 24 | Retained, hidden workspaces rebuild full media-picker arrays on catalog signals | Background UI work and duplicated library data | Code | M |
| 25 | Logging formatting and per-record output overhead | Rendering/input overhead and contention at high event rates | Measured; native acceptance open | M |
| 26 | Managed import rereads originals for verification, separately for backup and primary copy | Slow imports from cards/external drives | Code | M |
| 27 | Import, photo export, and timeline rendering share one active job slot | A long job delays unrelated queued work | Conditional | M–L |
| 28 | Bulk culling persists one asset per helper process | Save backlog after large rating/flag operations | Code | M |
| 29 | Timeline cache reporting rereads other project JSON and stats cache files synchronously | Slow cache dialogs and pressure-response pauses | Code | M |
| 30 | Visual-search cache hits still decode, resize, and JPEG-encode each image first | Repeated searches spend time preparing already-indexed photos | Code | M |

## Evidence from existing measurements

The September 26 [workflow report](../perf-artifacts/20260926T130033.061440Z-perf-actual-app-workflow-8a764264/actual-app-workflow-report.json) and [manifest](../perf-artifacts/20260926T130033.061440Z-perf-actual-app-workflow-8a764264/manifest.json) record:

| Stage | Recorded result | Interpretation |
| --- | ---: | --- |
| Shell ready | 2,799 ms, one sample | Slow startup in this harness |
| Carousel route | 2,528 ms max; 2,285.8 ms reported p95 | Cold opening dominates the three-sample set |
| Carousel input dispatch | 2,644.4 ms max | The delay includes event-loop unresponsiveness |
| Timeline route | 3,435 ms max; 3,126.8 ms reported p95 | Loading is still slow in this historical run |
| Timeline input dispatch | 433.4 ms max | Better than the earlier synchronous route, still delayed |
| First mask-brush frame | 359 ms, one sample | First-use symptom deserves a separate gate |
| Subsequent mask-brush frames | 55.95 ms reported p95, two samples | Small sample; not a reliable population percentile |

These are **debug, source-QML harness** results. The driver defaults to offscreen rendering, and this run's logs show CPU rendering. Production loads the compiled QML module. The completion event was captured (`target_events_met=true`), but the manifest records process return code `-15`; this is timing evidence, not clean-exit or native production proof. See the [earlier investigation](actual-qml-workflow-performance-2026-09-23.md) for route-only isolation and unsuccessful loading experiments.

The September 25 interaction artifact at `/private/tmp/photo-editor-tone-fusion-loop/20260925T003204.495911Z-perf-interaction-workflow-4a657a40/` records exposure first-feedback p95 **274.75 ms** across ten iterations. Its log contains `metal-canonical-cpu-readable` publication with **30,720,000 bytes transferred to CPU** for a 1600×1200 output. This is an existing artifact inspected during this audit, not a new run.

The [September 7 checkpoint benchmark](recipe-checkpoint-performance-2026-09-07.md) already improved serialization substantially, but its accepted 4 MiB raster-mask case still took **37.651 ms p95** for a synchronous checkpoint. That benchmark predates the current outbox format, so use it to justify remeasurement rather than assume the same current timing.

## Findings and improvements

### 1. Catalog loading blocks on a media probe for every video/audio asset

**Path:** [snapshot completion](../apps/desktop/src/catalog_asset_model.cpp#L4750) calls [parse_assets](../apps/desktop/src/catalog_asset_model.cpp#L1459), which calls [probe_video_duration_seconds](../apps/desktop/src/catalog_asset_model.cpp#L707) for each video and audio row. Each probe starts `ffprobe` and waits up to 5,000 ms. This happens before the snapshot is published on the model/UI thread. The timeout is a worst-case bound per probe, not a measured normal duration.

**Improve:** persist duration during import/media analysis, keyed by source revision. Display cached or unknown duration immediately. Queue missing/stale metadata to bounded workers and update only affected roles. Deduplicate probes across Library and Timeline.

**Measure:** cold and warm refresh of 1,000 mixed-media assets, including slow/unavailable files; record UI input-delay max and helper count. A warm refresh should start zero duration probes.

### 2. Project organization performs blocking helper calls per selected asset

**Path:** QML [Library project actions](../apps/desktop/qml/workspaces/LibraryView.qml#L814) call synchronous model methods. [addAssetsToProject](../apps/desktop/src/catalog_asset_model.cpp#L6501) loops through IDs and calls `runCatalogStateCommand` for each; [run_catalog_state_process](../apps/desktop/src/catalog_asset_model.cpp#L74) waits in the calling thread. Its start/finish limits are 5/15 seconds. Removing selected items also loops through blocking operations. An async single-asset API already exists, but these bulk callers do not use it.

**Improve:** add transactional batch membership commands and execute them asynchronously. Publish one bounded model update after acknowledgment; preserve existing failure/recovery behavior.

**Measure:** add/remove 1,000 assets while scrolling. Expect one batch operation, responsive input, and correct membership after relaunch and injected failure.

### 3. Carousel's asynchronous-looking export still blocks inside each step

**Path:** the [12 ms export timer](../apps/desktop/src/carousel_document_controller.cpp#L2312) invokes [processNextExportStep](../apps/desktop/src/carousel_document_controller.cpp#L19228) on the controller thread. [exportBoardToPlan](../apps/desktop/src/carousel_document_controller.cpp#L17765) renders a board, writes the image, then decodes the exported image for verification before returning. [Video export](../apps/desktop/src/carousel_document_controller.cpp#L22680) waits for FFmpeg with a 120-second bound. The timer yields between boards; it cannot make these operations nonblocking.

**Improve:** capture immutable export inputs and render/encode/verify them on a worker. Use process completion signals for video. Keep progress/cancel requests on the UI thread and make cancellation observable during a board export. Preserve output verification and partial-output policy.

**Measure:** export a multi-board design and a video slide while dragging/typing; record input latency and cancel-to-worker-stop time, not just export throughput.

### 4. CPU consumers force expensive photo-preview readback

**Path:** [render request construction](../apps/desktop/src/editor_preview_controller.cpp#L41018) requires a CPU preview for comparison, diagnostics, pixel inspection, and any layer containing `paintTiles`. [Canonical publication](../apps/desktop/src/editor_preview_controller.cpp#L12243) falls back to a CPU-readable image when it cannot keep the native output surface. The historical Metal artifact above demonstrates a substantial transfer and long first feedback. Native surface publication already exists; this is a remaining branch problem.

**Improve:** retain the composited GPU surface and update paint regions there. Read only the inspected pixel/region, and compute reduced scope data without downloading the whole image. Maintain a CPU base only for operations that actually need it. Track separate reasons for each readback rather than flipping `require_cpu_preview_image` blindly.

**Measure:** identical slider/paint/comparison workflows on native Metal; compare first-presented-frame p95, CPU materialization bytes, and final pixels. A readback optimization must demonstrably reduce transfers on the exercised branch.

### 5. CPU photo-effect rendering scales with both pixels and blur radius

**Path:** [convolve](../engine/src/pipeline/photo_effects.cpp#L102) runs serial nested loops over pixels and kernel taps; Gaussian blur, unsharp mask, and Optical Diffusion use this route. [Padded tiles](../engine/src/pipeline/photo_effects.cpp#L645) are also processed serially. Layer effects enter it through [run_layer_effects](../engine/src/pipeline/color_layer_runtime.cpp#L999). Separately, the desktop [GPU-support decision](../apps/desktop/src/photo_render_utils.cpp#L597) rejects nonempty photo layers on that render route. This does not mean every editor layer operation lacks GPU support; the editor has other fused paths.

**Improve:** implement parity-tested GPU effect stages; use bounded parallel tiles/SIMD for the CPU fallback. Cache unchanged upstream stages and reuse scratch storage. Large-radius algorithm changes or preview approximations require explicit image-quality validation; do not silently change the final effect.

**Measure:** 24/45 MP photos at small/default/large radii; separate draft latency, settled latency, export time, cancellation, peak memory, and CPU/GPU image parity.

### 6. Catalog refresh moves and transforms the whole library

**Path:** [snapshotArguments](../apps/desktop/src/catalog_asset_model.cpp#L7230) requests limit `0`. Rust [list_asset_snapshots](../rust/crates/catalog/src/lib.rs#L994) fetches all joined asset/recipe rows. [catalog_snapshot](../rust/crates/catalog/src/bin/catalog_snapshot.rs#L33) serializes the whole snapshot. C++ [handleSnapshotFinished](../apps/desktop/src/catalog_asset_model.cpp#L4750) parses the JSON, projects every recipe, and rebuilds model metadata synchronously. Fixing finding 1 removes one particularly bad part of this larger path.

**Improve:** page lightweight asset metadata with stable ordering; fetch full recipes on selection or editing demand. Use revision-based incremental refresh. Parse/normalize on workers and publish bounded row batches. Compact serialization can help transport but will not solve the whole-library algorithm.

**Measure:** 1k/10k/100k catalogs with realistic raster-heavy recipes. Track first usable viewport, total refresh time, payload bytes, UI-thread time, and peak memory.

### 7. Accepted edits synchronously rewrite the entire recovery outbox

**Path:** [persistCurrentState](../apps/desktop/src/editor_preview_controller.cpp#L41624) checkpoints before the 350 ms catalog-save debounce. [Journal write](../apps/desktop/src/editor_recipe_save_journal.cpp#L173) rebuilds pending/active/rejected recipe JSON, hashes it, and commits an atomic file. Keeping rejected recovery data is correct, but a large retained entry is reserialized during unrelated saves. Transactions already coalesce edits; that does not remove accepted-edit checkpoint cost.

**Improve:** store immutable raster/paint blobs once and journal small recipe revisions referencing them. Consider an append journal with crash-safe compaction and a durable acknowledgment boundary. Preserve exact recovery bytes, authority, ordering, checksums, and rejected-record isolation. Moving the write after acceptance without a durability protocol would lose the current guarantee.

**Measure:** small recipes, 1/4/16 MiB raster recipes, multiple pending assets, and a large rejected entry. Record UI duration, checkpoint bytes, and crash/reopen equality.

### 8. Cold workspace readiness needs a fresh production baseline and further splitting

**Path:** [Main's Carousel loader](../apps/desktop/qml/Main.qml#L3088) loads the large workspace synchronously; [Timeline](../apps/desktop/qml/Main.qml#L3113) now uses an asynchronous loader and has additional lazy subpanels. Historical reports still show seconds of route readiness cost. Existing changes mean old timings cannot establish the current remaining size of this problem.

**Improve:** first measure the compiled production module on native rendering. Split expensive first-use content into small independently activated components, keep the first usable controls cheap, and instantiate offscreen dialogs/panels only on demand. Use an actual interaction/idle signal for any prewarming. Earlier blanket asynchronous-loading trials regressed readiness; do not assume changing one Loader flag is sufficient.

**Measure:** separate process-cold and warm visits, first usable controls, final readiness, frame gaps, queued-input delay, and retained memory after visiting every workspace.

### 9. Frame cancellation can starve Timeline presentation

**Path:** [handleVideoFrame](../apps/desktop/src/timeline_playback_controller.cpp#L2880) increments `latest_frame_work_generation_` for every incoming frame, even when processing is active. The worker receives that generation as its cancellation token. [Completion](../apps/desktop/src/timeline_playback_controller.cpp#L2040) only publishes an image whose generation is still latest. If processing repeatedly exceeds the decoder's frame interval, each running frame can be cancelled or discarded before presentation. This is a source-demonstrated condition, not a reproduced runtime failure in this audit.

**Improve:** separate seek/source/grade invalidation from ordinary frame arrival. Keep only the newest pending frame, but allow the active frame to complete and present if its source/grade epoch is current. Drop superseded pending frames and adapt preview resolution when behind.

**Measure:** 30/60 fps playback with deliberately slower processing; assert continued frame presentation and bounded queue depth. Seeking and changing grades must still discard genuinely stale output.

### 10. Carousel preview generation occupies the UI thread

**Path:** the [preview timer](../apps/desktop/src/carousel_document_controller.cpp#L2298) calls `refreshCurrentSelectionPreviews`. [refreshPreviews/refreshPreviewAt](../apps/desktop/src/carousel_document_controller.cpp#L23979) iterate items/boards, decode edited images, paint previews, and [write PNGs](../apps/desktop/src/carousel_document_controller.cpp#L24918). Debouncing limits frequency but not the duration of each callback.

**Improve:** capture immutable board render descriptions; render affected visible boards on workers with revision checks. Publish previews through an in-memory provider and write disk caches in the background. Retain the existing affected-board optimization and extend it to dirty regions where worthwhile.

**Measure:** drag/resize/edit across 10/50/200 boards, record controller callback duration, first visible preview, stale result discards, and disk writes.

### 11. Timeline color preview repeatedly crosses the CPU image boundary

**Path:** [convert_yuv_video_frame_to_image](../apps/desktop/src/timeline_playback_controller.cpp#L1004) maps a `QVideoFrame`, converts samples to a `QImage` with serial loops, and [process_video_frame](../apps/desktop/src/timeline_playback_controller.cpp#L1391) performs further camera/grade/effect/composite passes. [Publication](../apps/desktop/src/timeline_playback_controller.cpp#L2952) exposes another image for display. Monitor-sized sampling, compiled transforms, LUT caches, and a bounded pending frame already exist.

**Improve:** retain decoder textures through YUV conversion, grading, and display using the native GPU path; fuse compatible stages and reuse surfaces. Preserve range, transfer, matrix, orientation, and preview/export color agreement. Until then, use a measured adaptive processing size and bounded SIMD/parallel CPU conversion.

**Measure:** 1080p/4K, 8/10-bit, 30/60 fps with camera transforms and adjustment grades; record presented fps, decode/grade/upload time, dropped frames, color error, and memory bandwidth.

### 12. CPU concurrency is fragmented across schedulers

**Path:** [working-image packing](../apps/desktop/src/working_image.h#L221) starts `std::async(std::launch::async)` tasks up to hardware thread count per sufficiently large conversion. The [color scheduler](../engine/src/pipeline/color_adjustment_scheduler.cpp#L21) owns another hardware-sized worker set. Preview tile work uses the [global Qt pool](../apps/desktop/src/editor_preview_controller.cpp#L11475), also used by video, exports, and other jobs. Brush and catalog-state pools already provide some isolation.

**Improve:** reuse persistent workers, set a common process CPU budget, reserve capacity for UI/interactive work, and keep background tasks cancellable. Avoid nested fan-out and prioritize urgent preview work ahead of exports/prewarming. Do not assume more threads improve throughput; measure on actual hardware.

**Measure:** slider and brush p95 while importing, exporting, and playing video; track runnable threads, queue wait, CPU utilization, and throughput. No deadlock is claimed from the source inspection.

### 13. Import preflight does recursive I/O on the UI thread

**Path:** [ImportView.refreshImportPreflight](../apps/desktop/qml/workspaces/ImportView.qml#L360) calls the model synchronously. [importPreflight](../apps/desktop/src/catalog_asset_model.cpp#L2699) recursively collects candidates for whole-folder imports, builds a full catalog path set, and makes repeated file-info passes. Policy changes rerun this path. [startImport](../apps/desktop/src/catalog_asset_model.cpp#L3080) can also collect all importable paths for project/preset assignment.

**Improve:** scan once on a worker, publish progressive counts, and cache immutable file facts by scan revision. Recompute policy-dependent results from that snapshot. Cancel obsolete scans and invalidate facts when storage changes. Keep the final import's integrity and duplicate checks authoritative.

**Measure:** 10k/100k files on internal, external, and unavailable storage; measure time until dialog responds, input latency, metadata calls, and policy-change latency.

### 14. Export preflight is too expensive for a synchronous UI binding

**Path:** [currentPreflight/currentBatchPreflight](../apps/desktop/qml/workspaces/ExportView.qml#L431) call [exportPreflight](../apps/desktop/src/export_job_controller.cpp#L890) and its per-asset batch loop. Work includes source dimensions, filesystem checks, recipe parsing, ICC/DCP resolution, and [RAW metadata reads](../apps/desktop/src/export_job_controller.cpp#L1110). Multiple bindings/helpers can request the report as destination, preset, selection, and scope counts change.

**Improve:** provide a cached asynchronous preflight model keyed by recipe/source/preset/destination revisions. Separate fast presentation checks from expensive source/profile checks; debounce text changes and deduplicate identical requests. Revalidate output collisions and other mutable conditions when jobs are admitted.

**Measure:** edit destination/preset with 1/100/1,000 selected RAWs; count preflight executions and source reads and measure typing latency.

### 15. Memory management does not cover all photo caches as one budget

**Path:** [photo cache constants](../apps/desktop/src/editor_preview_controller.cpp#L335) allow 512 MiB linear, 768 MiB adjusted, 256 MiB rendered, 256 MiB rendered tiles, 64 MiB instance maps, and 128 MiB mask tiles. The [decoded cache](../apps/desktop/src/decoded_image_cache.cpp#L15) defaults to another 512 MiB and is configurable. These are ceilings, not measured allocations. [Timeline pressure handling](../apps/desktop/src/timeline_document_controller.cpp#L22347) trims the decoded cache and Timeline resources, but not all those photo caches, history, and live render scratch allocations.

**Improve:** coordinate cache and in-flight working allocations under one hardware-aware budget with pressure notifications, per-workflow reservations, and observable eviction reasons. Preserve currently displayed surfaces while evicting reproducible background data. Avoid independently increasing each cache.

**Measure:** repeated 45 MP photo switching with layers and Timeline/Carousel visited; track live bytes by owner, peak RSS, memory pressure, evictions, cache hit rate, and decode repetition.

### 16. First mask-brush feedback needs its own acceptance budget

**Path:** the historical workflow reports a 359 ms first stroke, while [the benchmark](../tests/actual_app_workflow_benchmark_driver.cpp#L6822) puts first-use feedback in a separate summary. [The default gate](../scripts/perf/run-actual-app-workflow-gate.py#L33) budgets the ordinary brush summary only. Current [brush dispatch](../apps/desktop/src/editor_preview_controller.cpp#L43195) already uses an immediate QML vector overlay and a dedicated worker for the matte; blindly moving raster work again would miss what is already fixed.

**Improve:** add a first-use gate and measure pointer handling, state capture, overlay object creation, scene-graph upload, first frame, and worker completion separately. Warm only proven immutable prerequisites at safe idle points. Keep first feedback independent of the high-fidelity matte.

**Measure:** native first stroke after process start, after photo switching, and after enabling auto-mask, followed by warm strokes. The separate September 26 pixel-paint receipt does not establish mask-brush latency.

### 17. A catalog refresh invalidates unchanged thumbnail presentation

**Original path:** [updateAssets](../apps/desktop/src/catalog_asset_model.cpp#L5247) cleared thumbnail sources for online assets on each replacement. The scheduler was already bounded to the current viewport; the gap was losing reusable publication state. Disk cache hits could prevent rendering but did not eliminate scheduling, validation, and re-publication. Correction: the helper called by `reconcileTimelineAssets` already skipped Timeline rows, so the original claim that it cleared their previews was inaccurate. See current status above for the implemented preservation path.

**Improve:** merge incoming rows by ID and preserve thumbnail sources when source, recipe, renderer, and output-size revisions match. Invalidate only changed rows and retain offline previews.

**Measure:** repeated no-change refresh and one-asset edits. No-change refresh should cause no thumbnail generation or redundant image publication; an edit should invalidate only dependent thumbnails.

### 18. Timeline catalog reconciliation has avoidable quadratic work

**Status:** implemented with isolated measured acceptance; native workflow acceptance remains open. See current source status and the progress document.

**Original path:** [reconcileTimelineAssets](../apps/desktop/src/catalog_asset_model.cpp#L6033) searches the full asset vector with `find_if` for each incoming timeline, rebuilds several metadata collections, and rebuilds the visible ID index after each insertion. With N assets and T incoming timelines, these pieces approach O(N×T); the existing ID maps are not used for that initial merge.

**Improve:** use an ID-indexed merge, detect unchanged timelines, batch contiguous insertions/removals, and update indexes once. Recompute only metadata affected by actual changes. Preserve selection and row stability.

**Measure:** 10k photos with 10/100/1k timelines; compare no-op refresh and one-timeline modification, timing merge, metadata, signal emission, and thumbnail scheduling separately.

### 19. Filtering resets the entire Library model

**Status:** unresolved after three rejected candidates; original application implementation restored. See the progress document for measurements and the separate Studio gallery integration requirement.

**Path:** [applyFilters](../apps/desktop/src/catalog_asset_model.cpp#L5710) scans `all_assets_`, copies matching asset records, optionally sorts them, then uses `beginResetModel/endResetModel`. Even a small filter or culling change can recreate visible delegates and redo selection/stats. Search debounce already exists and the gallery is virtualized.

**Improve:** keep immutable asset storage plus a filtered ID/index view, use incremental row changes or an appropriate proxy, and move expensive filtering off-thread with revision checks. Precompute normalized search fields where profiling supports it. Pair this with server-side paging for very large catalogs.

**Measure:** typing and culling at 10k/100k assets; record filter CPU time, delegate recreation, scroll/selection preservation, and frame latency.

### 20. Photo history can retain large raster revisions without a byte budget

**Status:** tested storage foundation and partial controller integration exist. Integrated memory acceptance, remaining byte accounting, named-snapshot persistence, and native acceptance remain open; original count limits also remain.

**Path:** `pushUndoState()` captures detached immutable document values. Serialization remains on restore and transaction no-op comparison. `writeAutosave()` now captures a snapshot and submits full serialization/recovery writing to a worker, with one pending replacement and guarded completion. Save/discard/load/close drain admitted writes before changing recovery ownership. See [measured progress](performance-fix-progress-2026-09-28.md#third-retained-change-bounded-worker-autosave).

**Improve:** store immutable content-addressed raster/paint blocks shared across revisions, track retained bytes, and spill older undo data to recoverable storage. Persist named snapshots per asset/revision. Keep history semantics and user-created snapshots intact.

**Measure:** 100 raster-changing edits on several 45 MP assets, plus many named snapshots; measure actual uniquely retained bytes, snapshot-save time, undo latency, and exact restoration.

### 21. Metal scratch allocation and broad locking can serialize renders

**Status:** shorter cache locking has isolated measured acceptance; scratch reuse and native desktop acceptance remain open. See the progress document and raw evidence.

**Original path:** [publish_masks_and_color_lut_surface](../engine/src/pipeline/metal/metal_pipeline_backend.mm#L4320) holds `color_lut_mutex` through a large scope that allocates pixel/geometry/intermediate/spatial buffers, uploads pixels, and encodes work. Some mask/LUT buffers are cached already. Large per-render scratch allocations remain, and the broad mutex can make overlapping users wait for work unrelated to cache lookup.

**Improve:** snapshot immutable cached resources under short locks, allocate/reuse per-flight scratch surfaces from a bounded pool, and release resources after GPU completion. Keep resources alive across command execution and avoid unsafe sharing between concurrent renders.

**Measure:** allocator and lock-wait traces with interactive preview plus thumbnails/export; track scratch reuse, command submission delay, peak GPU/CPU memory, and rendered parity.

### 22. Timeline undo copies the entire project for local operations

**Status:** immutable clip-array reuse has isolated controller acceptance; changed tracks still copy clip objects and full value comparison remains linear. Byte budgets and native acceptance remain open.

**Original path:** [copyTracksForSnapshot/captureSnapshot](../apps/desktop/src/timeline_document_controller.cpp#L43523) rebuild every track/clip container. [pushUndoSnapshot](../apps/desktop/src/timeline_document_controller.cpp#L43653) invokes this for undoable edits. The adaptive maximum depth already limits clip-entry counts, but does not make capturing one edit proportional to the affected clips.

**Improve:** use operation-level undo or structurally shared immutable tracks/clips, with checkpoints for complex bulk operations. Budget retained bytes as well as entry counts. Keep coalesced drag transactions and external-document restoration correct.

**Measure:** trim/move/keyframe edits on 100/1k/10k clips; record input-to-state time, snapshot time, memory, and undo/redo equivalence.

### 23. Carousel undo and autosave serialize complete documents

**Status:** unused identity probes, immutable capture, bounded worker autosave, and exact transaction/saved-state comparisons are retained with measured gains. Restore latency, broader memory/lifecycle work, and native acceptance remain open; 24 is the next untouched priority.

**Path:** `pushUndoState()` captures immutable values. `writeAutosave()` submits full serialization/recovery writing to a worker with one pending replacement and guarded completion. `commitHistoryTransaction()` and `updateDocumentDirty()` compare record inputs with exact serialization/unknown-field fallback. Changed passthrough and undo restoration still materialize full JSON; lifecycle operations synchronously drain admitted writes. See [measured progress](performance-fix-progress-2026-09-28.md#fourth-retained-change-exact-document-comparisons).

**Remaining:** reduce restore and exceptional full-comparison work, add history byte accounting, and verify lifecycle/native recovery/UI behavior. Full-document disk writes remain.

**Measure:** native 50/200-board workflows, saved-document undo/redo, large paint/cutout and independent-source workloads, lifecycle waits, disk bytes, and real crash/relaunch recovery.

### 24. Hidden workspaces still rebuild full Library data projections

**Original path:** [Main loaders](../apps/desktop/qml/Main.qml) remain active after their workspace has loaded. Carousel catalog connections repeatedly rebuilt full picker maps even when hidden; Timeline already gated closed-picker work but rebuilt once per visible notification.

**Implemented and measured in isolation:** [Carousel](../apps/desktop/qml/workspaces/CarouselView.qml) and [Timeline](../apps/desktop/qml/workspaces/TimelineView.qml) mark hidden data dirty, refresh once on opening, and coalesce visible catalog bursts. [mediaPickerAssets](../apps/desktop/src/catalog_asset_model.cpp) constructs nine-field rows without recipe projection and filters unwanted media before mapping. The old full-map API remains available. Project changes, selection identity, metadata freshness, removal, and hide/close cancellation have focused coverage. Across five 10k-asset bursts, Carousel hidden scans fall 20 → 0, Carousel visible scans 25 → 5, and Timeline visible scans 20 → 5; Timeline hidden scans remain zero. See [raw comparisons and gates](evidence/performance-2026-09-28/workspace-picker-refresh.json).

**Remaining:** opening still builds a complete list, and closed pickers retain it. Incremental/paged publication, release of hidden presentation data, and native import/cull workflows after visiting both workspaces remain open. The synthetic offscreen fixture does not prove native frame/input behavior or real edited-asset workloads.

### 25. Diagnostic logging adds synchronous work to every emitting thread

**Original path:** the Qt handler serialized callers through a global mutex while formatting/parsing, writing, and flushing each JSONL record. Console formatting also ran for text later suppressed or replaced. This path is enabled in production through local diagnostics.

**Implemented and measured in isolation:** [ObservabilityWriter](../apps/desktop/src/observability_writer.h) owns one file thread, batches up to 64 records, and coalesces sparse routine output for up to 2 ms. It bounds pending work by 4,096 records / 8 MiB estimated captured data, including active batches, with one oversized record admitted alone. Capacity pressure blocks producers rather than discarding records. [The Qt handler](../apps/desktop/src/observability.cpp) captures owned producer context and reuses prepared structured fields without bypassing Qt filters/custom handlers. Warnings, critical/fatal messages, explicit flushes, and shutdown wait for delivery. File failures preserve complete fallback batches on stderr. [Raw comparisons and contracts](evidence/performance-2026-09-28/observability-writer.json) cover five modes, one/four producers, exact records/order, bounded admission, exit/fatal delivery, context lifetime, and file-open failure.

**Remaining:** native input/render timing, real terminal output, slow/full storage and partial writes, and native teardown remain unverified. Full JSON console formatting still has a producer cost. Backpressure and critical flushes can block on slow sinks, and abrupt termination can lose queued routine records; these flushes do not perform `fsync`. Sampling/aggregation policies are not introduced.

### 26. Managed import rereads source media for each verified destination

**Path:** [copy_file_verified_atomically](../rust/crates/catalog/src/importer.rs#L414) first copies, then independently hashes source and destination. [Backup and managed-copy loops](../rust/crates/catalog/src/importer.rs#L268) invoke it separately, so importing with a backup can read a source repeatedly from slow removable storage. Metadata extraction and database batch insertion already have useful parallel/batch implementations.

**Improve:** hash while streaming source bytes, reuse a validated source digest for primary/backup copies, and independently verify each destination. Detect source mutation across the operation and preserve atomic publication. Use bounded I/O concurrency appropriate to the storage; do not remove copy verification.

**Measure:** managed import with/without backup from a card/external drive; compare physical bytes read, elapsed time, UI responsiveness, and corruption/mutation detection.

### 27. A single active job delays unrelated job classes

**Path:** [processNextJob](../apps/desktop/src/job_runner.cpp#L2989) returns whenever `active_job_id_` is set; dispatch shares that slot across import, photo export, and timeline render. This conservatively limits resource use, but one long video render can delay an unrelated import or small photo export.

**Improve:** introduce resource-aware lanes with explicit CPU/GPU/I/O/memory admission and bounded concurrency. Permit genuinely independent lightweight work while a long job runs; retain one heavy GPU job if that protects interactive latency. Combine with finding 12 rather than simply increasing worker count.

**Measure:** submit a small export/import behind a long timeline render; compare queue wait, throughput, interactive p95, memory, and durable ordering/cancellation.

### 28. Bulk culling starts a separate helper process per asset

**Path:** [startNextCullingPersistence](../apps/desktop/src/catalog_asset_model.cpp#L6695) starts one `catalog_state set-review-state` process for each queued asset, serially. Same-asset updates are coalesced and QProcess execution is asynchronous, so this is chiefly a save-throughput/backlog issue rather than the direct UI freeze in finding 2.

**Improve:** drain bounded batches into one transactional helper request or a persistent local service, maintaining revision ordering and per-asset error reporting. Apply the same design to other per-asset persistence queues where measured overhead warrants it.

**Measure:** rate/flag 1,000 assets, record process launches and time until durable, then verify state after interruption/restart.

### 29. Cache diagnostics do expensive discovery synchronously

**Status:** background cached reports, incremental saved-document reference indexing, and asynchronous cleanup are implemented with accepted isolated gates. The path below describes the original finding. Native/full regression acceptance remains open; see the progress document.

**Path:** [cacheBudgetState](../apps/desktop/src/timeline_document_controller.cpp#L22224) calls `projectCacheSummary`. The [summary](../apps/desktop/src/timeline_document_controller.cpp#L22378) collects generated artifacts, stats files, and calls [otherProjectCacheReferences](../apps/desktop/src/timeline_document_controller.cpp#L22071), which recursively reads/parses other project JSON. QML requests these reports directly; critical pressure handling can also reach this work.

**Improve:** maintain an incremental artifact/reference index and publish cached reports from background discovery. Revalidate shared references at deletion time. Avoid a heavy synchronous scan while the app is already under memory pressure.

**Measure:** opening cache controls with hundreds of saved timelines and thousands of artifacts; record UI-thread time, files read, report age, and cleanup safety.

### 30. A visual-search cache hit still pays full image preparation

**Status:** persistent revision aliases now bypass preparation on valid saved-description hits, with accepted matched controller gates and cache contracts. The path below describes the original finding. Native/real-model acceptance remains open.

**Path:** [VisualSearchController::next](../apps/desktop/src/visual_search_controller.cpp#L620) calls `prepare_image` before `VisualSearchIndex::lookup`. [Preparation](../apps/desktop/src/visual_search_controller.cpp#L36) decodes, color-converts, scales, JPEG-encodes, and base64-encodes the image to derive its description key. Thus a warm description cache saves model inference but not image preparation; the batch advances serially. The separate embedding search already exists and should not be reimplemented.

**Improve:** maintain a validated mapping from asset/source/recipe/thumbnail/model revision to the prepared-image digest and saved description. Check that mapping first, prepare images only on misses, and prefetch a small bounded number when needed. Include edited-photo revisions so cached descriptions do not become stale.

**Measure:** cold indexing versus repeated searches over the same 1,000 edited photos; count decodes, encodes, inference requests, cache hits, and time to first/full results.

### 31. PNG compression dominates full-resolution photo export

**Status:** discovered during tool-effect verification and fixed in the current tree. Historical finding IDs remain unchanged. This was the next measured export bottleneck in the tool-only queue.

**Measured impact:** on a 25.8 MP portrait, default PNG encoding/publication took 13.2–16.2 seconds while effect rendering took 1.4–2.0 seconds. The atomic photo-export and standard buffered export writers now share explicit fast lossless PNG compression. Complete exports improve from 17.643 to 4.326 seconds (Gaussian) and 15.235 to 4.978 seconds (Optical Diffusion) across three alternating pairs per effect.

**Tradeoff and proof:** files grow 5.85–10.13%; all decoded 16-bit pixels and non-IDAT PNG chunks match exactly, memory stays within the guard, 48 encoder cases and cancellation/recovery tests pass, and the desktop builds. JPEG/TIFF behavior is unchanged. These timings cover the production flattened renderer and atomic writer; queued-job throughput, native dialog responsiveness, other photos, and other platforms remain unmeasured.

**Implementation:** [shared export PNG policy](../apps/desktop/src/export_image_encoder.h), [atomic photo-export writer](../apps/desktop/src/photo_export_task.h). [Matched evidence](evidence/performance-2026-09-28/png-export-encoding.json).

### 32. Opening Compare rerenders the unchanged edited photo

**Status:** measured after the user unlocked the native session and fixed in the
current tree. Historical finding IDs remain unchanged.

**Path:** the comparison branch skipped the complete rendered-image cache even
though its current recipe was unchanged and its reference rendered separately.
On the measured Optical Diffusion photo, this repeated about 470 ms of work.

**Improvement:** reuse the existing complete CPU-rendered edited image under its
normal identity while retaining independent reference rendering. Native-output
tagged entries and scope/partial/proof paths remain guarded. No cache budget
increase or additional comparison cache is needed.

**Proof:** three alternating foreground Cocoa/Metal pairs reduce split comparison
opening from 513 to 45 ms (91.2%). Preview/export hashes and comparison photo
regions are exact; all 24 captures are visibly populated. Regression and maximum
memory gates pass, two focused suites pass, and the desktop builds. Native
linked views, cold cache misses, large/RAW photos, and physical-pointer timing
remain outside this acceptance. The historical background drawable stall is
not claimed to be repaired.

[Implementation](../apps/desktop/src/editor_preview_controller.cpp),
[matched evidence](evidence/performance-2026-09-28/comparison-edited-cache.json).

## Existing improvements deliberately excluded as new issues

- Main Library galleries are virtualized; the old whole-catalog `Flow`/`Repeater` diagnosis is not a current recommendation.
- Thumbnail scheduling already follows the viewport and drops obsolete pending work; the old unbounded catalog queue should not be reported as still present.
- Metadata extraction uses workers and import database writes are batched in a transaction.
- Brush matte work already has a dedicated worker pool and immediate vector feedback.
- Native photo preview surfaces, in-memory publication, draft coalescing, tile/region paths, LUT caches, and trusted-Metal finite-validation skipping already exist.
- Timeline already clips its visible clip range, bounds its pending frame queue, caches LUTs, and has some pressure management.
- Main agent planning has an asynchronous continuation path. Legacy event-pump helpers alone are insufficient evidence that ordinary agent runs spin the UI thread.
- The Gaussian viewport already uses worker rendering, cancellation, progressive refinement, and native Metal texture publication. No separate high-confidence scene-only bottleneck was established here; it is still affected by shared CPU/memory contention.

## Suggested implementation order and proof

1. **Remove direct freezes:** findings 1–3, 10, 13, and 14. These have concrete UI-thread boundaries and can be improved without redesigning photographic color math. Preserve batch atomicity, recovery, and output verification.
2. **Fix interactive rendering:** findings 4, 5, 9, 11, 12, and 21. Use native Metal traces, current compiled binaries, frame presentation timestamps, and parity checks. Fix video generation semantics before interpreting dropped-frame performance.
3. **Control scaling and memory:** findings 6, 7, 15, and 17–24. Start with representative large libraries and raster-heavy edits, then change one cost at a time.
4. **Improve throughput and overhead:** findings 25–30. Use resource budgets so background throughput does not worsen editing latency.
5. **Rebaseline historical symptoms immediately:** findings 8 and 16. Use the current production module; keep process-cold, feature-first-use, and warm measurements separate.

Reuse [run-app-performance-suite.py](../scripts/perf/run-app-performance-suite.py), [run-actual-app-workflow-gate.py](../scripts/perf/run-actual-app-workflow-gate.py), the interaction workflow gate, recipe-save benchmark, and existing render/effect tests. Add coverage only where the identified path is absent. Every accepted optimization should include the exact workload/backend, before/after artifacts, one primary latency or throughput metric, frame/input and memory guard metrics, and relevant output/recovery correctness evidence. A headless CPU result cannot establish native Metal or physical-input smoothness.

No percentage speedups are promised from source inspection. Refresh the measurements against this checkout before assigning numerical savings or marking any finding fixed.
