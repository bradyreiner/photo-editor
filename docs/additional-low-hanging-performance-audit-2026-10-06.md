# Additional interaction performance issues — 2026-10-06

Five additional costs remain beyond the [previous audit](further-interaction-performance-audit-2026-10-06.md). All five were reproduced in isolated release-controller or actual QML probes. No application or test source was changed for this audit.

| Path | Fixture | Median measured cost | Narrow first improvement |
| --- | --- | ---: | --- |
| Timeline render-cache publication during empty-track addition | 100 clips, 400-second sequence, one cache-model subscriber | **1,584 ms** for the public command | Share serialization across range signatures; eliminate the second invalidation/publication |
| Carousel layer iteration | 60 mixed text, brush and shape items | **802 ms** direct QML reads; **6.7 ms** using one local collection read | Read `canvasItems` once before each loop |
| Timeline missing-media list | 2,000 clips with distinct missing local paths | **1,806 ms** | Count normalized paths in one pass |
| Timeline Versions list | 1,000 clips, five stored versions | **512 ms**; empty list with 10,000 clips still **742 ms** | Skip serialization when there are no candidates; separate list metadata from selected comparison |
| Mask housekeeping count | One canonical painted brush with 10,000 points | **46 ms** for three QML count reads | Cache the count by mask revision or query validated geometry without normalization |

These are measured CPU/binding costs, not native input-to-presented-frame measurements. Carousel's alternative loop exists only in the probe; no shipped optimization has been accepted.

## Evidence and method

[Evidence](evidence/additional-low-hanging-performance-2026-10-06/) includes [the probe](evidence/additional-low-hanging-performance-2026-10-06/probe.cpp), [aggregate measurements](evidence/additional-low-hanging-performance-2026-10-06/summary.json), 15 accepted result/log pairs, compile/link inputs, the successful runtime build log, required frozen headers, audited function bodies and QML trigger excerpts. Each accepted process uses one warmup and three timed samples per metric. Reported numbers are the median of three independent process medians. The summary retains each process median and its minimum/maximum; individual timed samples were not retained. Families ran sequentially without an overlapping audit build or benchmark.

The release `desktop_shell_test_runtime` was successfully rebuilt before the accepted runs and copied into `/tmp/looksy-wide-performance-audit-2026-10-06/runtime.a`. The temporary probe uses matching frozen application headers. All six controller source/header hashes matched the recorded build inputs immediately after that build. Concurrent Carousel work subsequently added a `Slide` field, so the measurements describe the recorded build snapshot, not a complete build of the later tree. [Post-run verification](evidence/additional-low-hanging-performance-2026-10-06/post-run-verification.json) confirms the nine audited function bodies still match the inspected paths. [Provenance](evidence/additional-low-hanging-performance-2026-10-06/accepted-probe-provenance.json) records the source, binary and header hashes. The runtime archive and executable remain in `/tmp`; they are not copied into the repository.

Fixtures use an isolated temporary app-data root. Timeline clips are synthetic, with four-second durations, four effects and one opacity keyframe each. Timeline measurements do not decode valid media, run the production workspace, measure journal draining or present Metal frames. The public track-add probe attaches one synchronous `renderCacheChanged` subscriber that reads the range list. QML probes use `QQmlEngine`/`QQmlComponent` with checked component creation and result-count assertions. They reproduce the relevant property access patterns, excluding unrelated UI layout and rendering.

## 1. Every render-cache range starts by serializing the entire timeline

[`renderCacheSignature()`](../apps/desktop/src/timeline_document_controller.cpp#L7745) begins with `documentObject()`, then filters tracks/clips for its range and hashes the result. [`expectedRenderCacheTasks()`](../apps/desktop/src/timeline_document_controller.cpp#L7825) calls it for every four-second segment whenever the task cache is dirty. That repeats whole-document serialization once per segment.

[`TimelineView.renderCacheRangeModel`](../apps/desktop/qml/workspaces/TimelineView.qml#L498) reads this list and feeds the timeline cache strips. An edit makes the task list dirty. [`addVideoTrack()`](../apps/desktop/src/timeline_document_controller.cpp#L19700) calls `emitAllState(true)` followed by `setDirty(true)`. Both dirty the cache and emit `renderCacheChanged`; a subscriber reading the list on both publications causes two full task-list reconstructions.

| Clips / four-second ranges | Invalidated range getter | Warm range getter | Empty-track command with subscriber |
| ---: | ---: | ---: | ---: |
| 10 | 10.9 ms | 0.016 ms | 24.2 ms |
| 30 | 81.2 ms | 0.048 ms | 175.0 ms |
| 60 | 308.0 ms | 0.094 ms | 607.7 ms |
| 100 | 790.9 ms | 0.156 ms | 1,584.4 ms |

The 100-clip fixture is a 6-minute-40-second sequence. This reaches an ordinary project duration rather than requiring thousands of clips. Warm reads are cheap; invalidation is the problem. The command includes other controller work, so its entire duration should not be attributed exclusively to signature calculation. The actual QML workspace may coalesce some binding work; its complete command latency needs native measurement.

First hypothesis: build one document snapshot per task-list reconstruction and share it across the existing range-signature calculation, then remove the duplicate invalidation/publication. Preserve signature bytes, source/LUT modification checks, project identity, transition preroll and cache ownership. More extensive interval indexing can follow if the remaining filtering cost warrants it.

## 2. Carousel loops repeatedly fetch every item's full native map

[`currentSlideLayers`](../apps/desktop/qml/workspaces/CarouselView.qml#L366) indexes `carouselDocument.canvasItems` directly inside its loop. [`guideCandidatesForSlide()`](../apps/desktop/qml/workspaces/CarouselView.qml#L2337) also reads the native property in the loop condition and body. The guide function runs at layer drag/resize start. The current-board layer list feeds the inspector and commands.

[`canvasItems()`](../apps/desktop/src/carousel_document_controller.cpp#L2690) constructs maps for every item on every getter read. `itemMap()` includes brush points and text layout/overflow fields. Direct reads inside the QML loop therefore repeatedly rebuild the entire collection.

| Items | Shape-only direct QML loop | Mixed direct QML loop | Mixed loop with one local read |
| ---: | ---: | ---: | ---: |
| 10 | 4.4 ms | 26.9 ms | 1.24 ms |
| 30 | 36.9 ms | 199.1 ms | 3.21 ms |
| 60 | 145.1 ms | 802.1 ms | 6.72 ms |

Mixed fixtures contain equal proportions of text, brush and shape items; text has eight repeated paragraph fragments at 24 points and brushes have 100 points. The actual QML probe checks `itemId` and returns the same item count in both versions. It reproduces collection access, not the complete board-intersection or snapping logic.

First hypothesis: assign `const canvasItems = carouselDocument.canvasItems` once at the start of each production function/binding and iterate that value. This is the clearest small change in this batch: the actual QML alternative already demonstrates that a local collection read avoids the repeated getter cost. Verify layer filtering, order, snapping and updates in the production workspace before accepting a fix.

## 3. The Relink button can trigger a quadratic missing-media scan

[`offlineMediaItems()`](../apps/desktop/src/timeline_document_controller.cpp#L10480) checks each clip's file readability, then scans every clip again for each unique missing normalized path to calculate `useCount`. It also normalizes every candidate path in each inner scan. This is proportional to missing unique paths multiplied by clip count.

[`TimelineSourcePanel`](../apps/desktop/qml/components/TimelineSourcePanel.qml#L1509) reads `offlineMediaItems.length` to decide whether to show Relink. The native property notifies on `clipsChanged`, so clip edits can retrigger this work while the source panel is loaded.

| Clips | All clips share one missing path | Each clip has a distinct missing path |
| ---: | ---: | ---: |
| 100 | 0.225 ms | 4.8 ms |
| 500 | 1.038 ms | 110.7 ms |
| 1,000 | 2.237 ms | 445.2 ms |
| 2,000 | 4.219 ms | 1,806.3 ms |

The probe asserts both returned item count and `useCount`. Paths are missing local files under the temporary fixture root; these numbers do not depend on network storage latency.

First hypothesis: count normalized path references once, then build the missing list using those counts. Preserve path equivalence, result ordering and external file availability changes. A lightweight missing-media boolean could serve the button, but availability caching needs explicit refresh/invalidation rules.

## 4. Versions computes detailed comparisons for the whole list

[`sequenceVersions()`](../apps/desktop/src/timeline_document_controller.cpp#L9531) serializes the current document before checking any version candidates, then reads/parses stored document files and computes detailed comparisons while constructing list entries. [`sequenceVersionComparison()`](../apps/desktop/src/timeline_document_controller.cpp#L9668) calls the whole list getter again to find one entry.

[`onSequenceVersionsChanged`](../apps/desktop/qml/workspaces/TimelineView.qml#L3629) refreshes the list when the Versions popup or program comparison is active. If a selected comparison exists, it then requests that comparison separately, repeating the list calculation. The existing visibility guard is real; this finding does not claim the closed popup always performs the work.

| Clips | Stored versions | List getter |
| ---: | ---: | ---: |
| 100 | 0 | 7.5 ms |
| 1,000 | 0 | 77.5 ms |
| 5,000 | 0 | 380.1 ms |
| 10,000 | 0 | 742.5 ms |
| 1,000 | 1 | 163.3 ms |
| 1,000 | 5 | 511.9 ms |

Stored versions are full fixture timeline JSON files in the isolated version root. The fixture has no saved-document or recovery path, so this excludes extra recovery/autosave candidate work. Timing measures the list getter only, not the second selected-comparison query.

First hypothesis: find candidate files before serializing the current document; return immediately when none exist. Then separate cached list metadata from detailed selected-version comparisons, and reuse a calculated comparison during a single UI refresh. Preserve version ordering, recovery candidates, delta accuracy and external file updates.

## 5. Asking whether empty selections exist renormalizes brush geometry

[`emptyMaskCount()`](../apps/desktop/src/editor_preview_controller.cpp#L19617) loops the mask stack and calls [`selection_mask_is_empty_or_invalid()`](../apps/desktop/src/mask_presentation_schema.h#L69). The helper normalizes each mask before checking its role/kind and painted content. A brush's point array is processed again even though the stack is already canonical.

[`StudioWorkspace`](../apps/desktop/qml/workspaces/StudioWorkspace.qml#L14707) independently reads the count for the Remove Empty Selections button's visibility and enabled state. The toolbar status reads it again. The property notifies on `controlsChanged`, allowing unrelated control updates to request the same derived count.

| Canonical brush points | One count read | Three QML count reads |
| ---: | ---: | ---: |
| 100 | 0.206 ms | 0.536 ms |
| 1,000 | 1.530 ms | 4.672 ms |
| 5,000 | 7.566 ms | 22.626 ms |
| 10,000 | 15.035 ms | 45.886 ms |

The fixture normalizes and installs the brush before timing. Every count must return zero, preserving the distinction between a painted brush and an empty selection. The QML probe reproduces three reads; it does not measure production binding scheduling. These costs exclude the previously reported mask-set grouping work, preview rendering and persistence.

First hypothesis: cache this count with the mask geometry revision, or query the validated representation without re-normalizing. Keep full validation at import/persistence boundaries and preserve invalid-selection handling. This is smaller than the Timeline/Carousel stalls but large enough to consume several frame budgets whenever those bindings reevaluate.

## Lower-priority measurements and excluded candidates

Carousel text-only model construction measured 36.2 ms for 120 text items, with preflight at 18.6 ms and copy proof at 3.9 ms. Photo-backed contrast checks measured about 16 ms for 120 text items across 20 boards using a generated 1,200 × 800 image. Reusing text layouts is worth investigating after fixing the repeated collection reads; these probes do not establish a native rendering bottleneck.

Standalone `QJSEngine::newQObject` iteration initially suggested severe live-brush and timeline-track list costs. Actual QML bound-list probes contradicted that result: a 1,000-point brush iteration took 0.049 ms, and a cached 50-track/1,000-clip iteration took 0.0013 ms. Production `readonly property var` list bindings have different caching behavior. Those proposed issues are excluded; [qualifying evidence](evidence/additional-low-hanging-performance-2026-10-06/ruled-out/) is retained separately from the accepted batch. Carousel's direct reads were tested in actual QML before being included.

Early exploration encountered an asynchronously loading QML component, then a temporary probe ABI mismatch caused by concurrent header edits, and an overly strict fixture track-count assertion. Those runs were discarded. Matching frozen headers, bounded component-load waits and corrected fixture assertions were used in all 15 accepted processes. None of those exploratory failures is reported as an application defect.

## Suggested order

Fix Carousel's direct collection reads first, then share render-cache serialization and remove duplicate publication, then make missing-media reference counting linear. Their triggers and narrow hypotheses are clear. Versions and mask housekeeping follow with explicit cache-invalidation checks. Each candidate still needs an identical rerun plus appropriate production behavior/parity evidence; this document is an audit, not an optimization acceptance report.
