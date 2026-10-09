# Deeper interaction performance audit — 2026-10-06

Six additional interaction paths were measured beyond the [previous batch](additional-low-hanging-performance-audit-2026-10-06.md). The strongest new findings are Timeline search and expensive mask-outline cache hits. Multicam and Paste Attributes have smaller, scale-dependent costs. This audit changed no application or repository test source.

| Path | Fixture | Median measured cost | First hypothesis |
| --- | --- | ---: | --- |
| Timeline search | 10,000 clips, all match | **821 ms** | Build the flattened clip-index map once per query |
| Cached mask-outline bindings | 10,000 brush points, already cached outline | **220 ms** to refresh the two QML properties | Avoid serializing mask geometry to find a cache hit |
| Color-mixer eyedropper | Generated 24 MP PNG, first full-resolution decode | **145 ms**; warmed command **1.9 ms** | Move decode off the UI thread and sample the required region without copying the whole crop |
| Timeline color scopes | One real generated 4K H.264 clip | **79 ms** per call | Run frame extraction asynchronously or reuse an appropriately identified processed frame |
| Paste Attributes comparison | 10,000 selected clips | **103 ms** | Use constant-time selection membership and reuse comparison work |
| Multicam list construction | 5,000 clips, 20 eight-angle groups | **88 ms** | Reuse clip references instead of rebuilding sorted references per angle |

These are isolated controller and QML costs, not production input-to-presented-frame measurements. No proposed fix was implemented or accepted. Workload sizes matter: the 10,000-clip and 20-group entries are stress cases; the first eyedropper decode and color-scope extraction also occur with one photo or one clip.

## Measurements and build provenance

[Evidence](evidence/deeper-interaction-performance-2026-10-06/) includes [the controller probe](evidence/deeper-interaction-performance-2026-10-06/probe.cpp), [the QML outline probe](evidence/deeper-interaction-performance-2026-10-06/outline_qml.cpp), [aggregate results and ranges](evidence/deeper-interaction-performance-2026-10-06/summary.json), raw result/log pairs, build/compile/link inputs, required frozen headers, audited function bodies, QML trigger excerpts and artifact hashes.

All 24 controller processes and three final QML processes exited successfully. Each warm metric uses one warmup and three timed samples per process; the report uses the median of the three process medians. Cold photo metrics use one newly generated file and one decode-cache-cold sample per process. Individual warm samples were not retained, and no population percentile is claimed. The nine workload families ran sequentially. Audit builds and probe compilation were complete before the corresponding accepted timing batches.

The release `desktop_shell_test_runtime` rebuilt successfully before these measurements. All six main controller source/header hashes matched the captured build inputs immediately afterward. The runtime archive was frozen with matching application headers. [Post-run verification](evidence/deeper-interaction-performance-2026-10-06/post-run-verification.json) confirms that all 15 audited controller function bodies still match that snapshot. [Provenance](evidence/deeper-interaction-performance-2026-10-06/provenance.json) records source and binary hashes. The runtime and executables remain in `/tmp/looksy-deeper-performance-audit-2026-10-06/`, outside repository evidence.

Each process uses an isolated temporary app-data directory. Synthetic Timeline fixtures have four-second clips, four effects and one opacity keyframe. There is no media decoding in the search, multicam or Paste Attributes fixtures. The scopes family separately generates valid two-second 1080p/4K H.264 files with FFmpeg and verifies decoded scope data. Photo fixtures are generated 3,000 × 2,000 and 6,000 × 4,000 PNGs. This does not qualify RAW behavior, native presentation, exports, or the complete production workspace.

## 1. Search rebuilds the sorted clip references for every matching result

[`timelineSearchResults()`](../apps/desktop/src/timeline_document_controller.cpp#L6011) adds a `clipIndex` to every matching clip by calling `flattenedIndexForClipId()`. That helper calls `orderedSelectableClipRefs()`, which collects and sorts the references again, then scans them for the requested ID. A broad query repeats that work once per result.

[`TimelineView.timelineSearchResultModel`](../apps/desktop/qml/workspaces/TimelineView.qml#L473) queries the controller reactively when clip, track or marker data changes. It has no inactive-search guard. The search bar's hidden Label and Prev/Next enablement consume `searchMatchCount()`, which consumes this model. Search highlighting separately guards the inactive case. Source inspection identifies these subscriptions; the native cost of hidden-bar reevaluation was not measured.

| Clips | All match | One match | No match |
| ---: | ---: | ---: | ---: |
| 100 | 0.26 ms | 0.04 ms | 0.03 ms |
| 1,000 | 9.60 ms | 0.37 ms | 0.29 ms |
| 5,000 | 210.28 ms | 2.35 ms | 1.97 ms |
| 10,000 | 821.11 ms | 5.52 ms | 4.30 ms |

The fixture asserts result counts and the first/last flattened indices. File paths are empty, so this excludes real filesystem or network-media availability costs. First hypothesis: build the ordered references and ID-to-index mapping once for each query. Also avoid requesting the complete default result list when the UI has no active search. Preserve ordering, track visibility, standalone audio, marker filtering and result navigation.

## 2. Outline cache hits hash the full active brush before checking the cache

[`activeMaskOutlinePaths()`](../apps/desktop/src/editor_preview_controller.cpp#L20298) constructs its fast cache key using `QJsonDocument::fromVariant(...).toJson()` and SHA1 over a payload containing the full active mask. Brush points, normalized stroke samples and matte bytes remain in that mask. This work happens before `outlineCacheLookup()` even on a confirmed hit. [`activeMaskOutlineTrace()`](../apps/desktop/src/editor_preview_controller.cpp#L20826) calls the paths getter again to produce diagnostics.

[`SharedMaskOverlay`](../apps/desktop/qml/workspaces/SharedMaskOverlay.qml#L55) binds the trace and, separately, the outline paths. The QML probe recreates two `readonly property var` bindings, invalidates their dependency together, and asserts that the resulting trace still says `cache-hit`. It measures binding refresh without canvas painting, a real source photo, or worker computation.

| Brush points | Warm native paths getter | Warm native trace getter | Refresh two QML bound properties |
| ---: | ---: | ---: | ---: |
| 100 | 1.16 ms | 1.12 ms | 3.46 ms |
| 1,000 | 7.32 ms | 7.14 ms | 22.90 ms |
| 5,000 | 35.33 ms | 36.14 ms | 108.95 ms |
| 10,000 | 70.58 ms | 70.53 ms | 220.08 ms |

The fixture is a normalized brush with preserved points/strokes, a 256 × 256 stored matte and smoothing enabled. With no source file, the controller produces and caches its direct fallback contour before timing. Cached paths must remain exactly equal and trace reads must report a hit. This measures cache admission and property publication, not final-alpha outline fidelity. The QML values are directly measured; they are not sums of the native getter medians.

A polygon control confirms that this cost depends on geometry size: a warm paths read grows from 0.15 ms at 100 points to 8.76 ms at 10,000. Brush records are more expensive. This is distinct from the earlier mask grouping and empty-selection counting findings: the remaining cost here is JSON/hash work on an already cached outline.

First hypothesis: reuse a geometry identity or revision-based key rather than serializing the mask on every lookup, and share one obtained outline/trace result where appropriate. Verify all mutation, undo, import, active-component, source, display-scale and preview-mode invalidation paths before removing data from the key.

## 3. Color sampling decodes and copies the full photo for a small patch

[`selectColorMixerChannelAt()`](../apps/desktop/src/editor_preview_controller.cpp#L39835) calls `decodeSourceImage(..., 0)`, then `applyCrop()` before sampling a patch with a maximum radius of 12 pixels. `apply_crop()` performs `image.copy(crop_rect_for_image(...))`, including when the crop is the entire image. The white-balance and color-replacement sampling commands use the same preparation pattern.

[`StudioImageViewport.handleSamplingPointerPressed()`](../apps/desktop/qml/components/StudioImageViewport.qml#L550) invokes these commands directly from pointer handling.

| Generated photo | Crop | First command with cold decode cache | Warm command |
| --- | --- | ---: | ---: |
| 6 MP | Entire image | 52.78 ms | 0.66 ms |
| 6 MP | Center half in each dimension | 34.99 ms | 0.44 ms |
| 24 MP | Entire image | 145.06 ms | 1.90 ms |
| 24 MP | Center half in each dimension | 131.68 ms | 1.01 ms |

The fixture preselects the expected Greens channel; every command must successfully retain it. This excludes a channel-change edit's history, persistence, preview submission and settling costs. The files were just written, so “cold” means the application's decode cache, not an OS filesystem cold start. Full-resolution decode dominates; this is not a claimed 145 ms penalty on every warmed click.

First hypothesis: prepare decoding asynchronously and map the required patch through the crop coordinates without copying all crop pixels. Retain full-resolution sampling and color interpretation; a lower-resolution sample is not an accepted substitute.

## 4. Color scopes launch FFmpeg and wait on the UI thread

[`analyzeSelectedClipColorScopes()`](../apps/desktop/src/timeline_document_controller.cpp#L31288) calls `decode_color_scope_frame()`, which starts FFmpeg, waits synchronously with a 15-second timeout, extracts/encodes a PNG, then constructs scope arrays. There is no request/result cache in this method.

[`TimelineView`](../apps/desktop/qml/workspaces/TimelineView.qml#L11921) invokes this when opening Color Finishing, after applying/removing a LUT or completing shot match, and from Refresh Scopes. No continuous playback timer is asserted for this path.

| Real generated source | Controller analysis |
| --- | ---: |
| 1080p H.264 | 59.82 ms |
| 4K H.264 | 79.26 ms |

The same selected frame is requested repeatedly. Fixtures have no effects or LUTs. Assertions require a positive decoded sample count, 64 histogram bins and complete 4,096-entry waveform/vectorscope arrays. These timings include process startup, extraction and scope construction; they do not isolate their shares. Slow codecs, storage and complex filters were not measured.

First hypothesis: move extraction and scope calculation to a worker with latest-request handling, or reuse a processed frame only when source/time/edit/color identity matches. Keep the existing scope interpretation and protect against stale results.

## 5. Paste Attributes pays for every selected target before the dialog opens

[`pasteAttributesComparison()`](../apps/desktop/src/timeline_document_controller.cpp#L21496) scans all clips using `isClipSelected()`, whose selected-ID lookup is linear. It then builds details and fingerprints for seven categories over the selected target list. [`openPasteAttributes()`](../apps/desktop/qml/workspaces/TimelineView.qml#L3965) requests this comparison synchronously.

| Clips in sequence | One selected target | All selected |
| ---: | ---: | ---: |
| 100 | 0.014 ms | 0.41 ms |
| 1,000 | 0.015 ms | 4.44 ms |
| 5,000 | 0.024 ms | 33.12 ms |
| 10,000 | 0.033 ms | 102.52 ms |

The probe installs one copied source clip and identical target attributes, checks exact target counts and all seven categories, and measures comparison only. It does not measure copy or paste execution. This is another remaining consumer of linear selection membership beyond the earlier projection/removal findings, with additional category-comparison work.

First hypothesis: form a selected-ID set once for the command and reuse target details/fingerprints where safe. A single-target comparison is already cheap; prioritize this for bulk attribute workflows.

## 6. Multicam constructs full group data using repeated sorted lookups

[`multicamGroupMap()`](../apps/desktop/src/timeline_document_controller.cpp#L36566) resolves each angle with `clipRefAt(flattenedIndexForClipId(...))`. Both operations obtain freshly ordered clip references. [`multicamGroups()`](../apps/desktop/src/timeline_document_controller.cpp#L5157) does this for the whole list, including callers that only ask whether its length is nonzero.

| Clips | One eight-angle group, list getter | 20 eight-angle groups, list getter | Eight monitor-state queries |
| ---: | ---: | ---: | ---: |
| 500 | 0.28 ms | 5.67 ms | 0.27 ms |
| 2,000 | 1.36 ms | 29.35 ms | 1.42 ms |
| 5,000 | 4.45 ms | 87.76 ms | 4.68 ms |

The monitor queries address one eight-camera group and must return valid, available states with the exact camera identities. [`TimelineMulticamAngleMonitor`](../apps/desktop/qml/components/TimelineMulticamAngleMonitor.qml#L240) requests those states on sequence-position changes. That cost is smaller than rebuilding the whole list; this evidence does not establish dropped video frames or decoding throughput. Active-group construction measured about 4.3 ms at 5,000 clips.

First hypothesis: resolve group angles from one ID-to-reference map per projection rather than repeatedly sorting all clips, and expose a cheap group count for enablement. Share the underlying reference work with other audited lookups while preserving visibility, angle identity, cuts, timing and audio mode.

## Additional observation: color-mask preview encodes before enforcing its byte cap

`colorToolMaskPreviewSource()` still decodes, crops, loops over pixels and encodes PNG on the UI thread. A valid cropped 512 × 171 preview measured 5.997 ms on a changed hue, 0.235 ms unchanged, and 144.63 ms on the first decode of the 24 MP source. The probe asserts a decodable PNG and changed output after each hue change. This overlaps the cold-decode mechanism above and is not counted as a seventh independent major bottleneck.

Uncropped 512 × 341 previews returned an empty source despite successful decoding. The [separate diagnostic](evidence/deeper-interaction-performance-2026-10-06/exploration/uncropped-color-preview-failure.log) records a same-size solid RGBA PNG of 699,923 bytes using the getter's writer settings, above its 512 KiB cap. The size check occurs after pixel processing and encoding. This strongly implicates the cap/writer settings and warrants a focused correctness check; the diagnostic does not expose the getter's internal encoded byte count. Those failed uncropped samples are excluded from accepted performance results. The successful preview fixture explicitly crops to half the height.

## Priority

Start with the mask-outline cache identity and one-pass Timeline search indices. The mask issue affects recurring overlay work with long strokes; broad search queries produce the largest measured stall in this batch. Then address foreground full-resolution sampling and synchronous scope extraction. Multicam and Paste Attributes are worthwhile bulk-project improvements, with lower costs in small projects. Verify a proposed fix with identical fixtures and production behavior evidence before claiming an accepted improvement.
