# Performance round 14 — November 13, 2026

No production optimization is retained from this pass. The strongest measured
opportunity is thumbnail-directory accounting: an eight-thumbnail burst used
87.4% less accounting CPU, but protected first-scan p95 regressed 6.52%, exceeding
the 5% cap. The candidate is reverted. Qt path caching was also rejected after a
precision difference. Five further current-source opportunities are ranked below;
no unmeasured speedup is claimed.

Artifacts: `perf-artifacts/performance-round14-2026-11-13/`.

## Thumbnail-directory accounting

Every completed thumbnail traversed the entire on-disk thumbnail cache to count
its files and bytes. This work runs after thumbnail generation, before the worker
publishes its result. A 25,000-file fixture took approximately 163–200 ms CPU per
scan. Across 24 thumbnail completions, accounting alone consumed about four
seconds of CPU. These are warm-filesystem accounting measurements, excluding
thumbnail decoding/rendering and native presentation.

The rejected candidate shares one thread-safe snapshot for up to one second. It retains
one root and one pair of totals; changing roots forces a fresh scan. Concurrent
workers share the scan. It does not cache images or modify thumbnail validity,
source revisions, recipes, pixels, queue priorities or cancellation.

The rejected integration retained the existing `thumbnail.completed` totals and
probe duration, and added these fields (none ship in the restored source):

- `thumbnail_cache_stats_sampled`: whether this request performed a scan.
- `thumbnail_cache_stats_age_ms`: snapshot age at the accounting request.
- `thumbnail_cache_stats_interval_ms`: the 1,000 ms sampling interval.

Totals are periodic observations. External changes appear on the next expired
sample; they are not represented as newly scanned totals on a cache hit.

The standalone contract covers additions, deletion, file-size changes, ignored
extensions/subdirectories, root switching, absent directories, expiry, clock
rollback and eight concurrent workers receiving consistent totals from one scan.

The initial fixed process comparison passed at 1,000 files. At 25,000 files its
baseline burst p95 controls drifted 8.88%, and scan CPU changed from approximately
163 to 200 ms during the experiment, including the final baseline. That entire
large-cache CPU comparison is ineligible; its roughly 95% burst improvement is
diagnostic. Candidate first-scan mean/p95 also exceeded the protected 5% limit in
that comparison. Neither failure is hidden or accepted as a passing comparison.

The final prospective protocol pairs A/A controls and B-C-C-B samples within one
process, using seven fixed samples per block and an eight-request burst. Numeric
gates remain: target mean/p95 at least 10% better; protected first-scan mean/p95 at
most 5% slower; CPU A/A mean/p95 variation at most 5%; independent process peak
RSS at most 10% higher. All samples count. Wall-clock eligibility is reported
separately. The identical sampler's process-RSS receipts from the first protocol
remain independent memory evidence; the paired process does not attribute RSS
to either algorithm. No further measurement redesign is allowed in this pass.

One earlier interrupted run overlapped a probe build; it is archived in full as
`thumbnail-1000-interrupted-build-overlap` and excluded. Final timings wait for
both Release and Debug build locks. Neither the active editor nor its catalogs
are stopped or modified by the probes.

The final paired protocol passed its CPU controls at both sizes. All final
samples are included in these median-of-block statistics:

| Cache files | Accounting workload | Baseline CPU mean / p95 | Candidate CPU mean / p95 | Decision |
| --- | --- | ---: | ---: | --- |
| 1,000 | Initial scan | 5.744 / 5.895 ms | 5.777 / 6.063 ms | Within protected cap |
| 1,000 | Eight requests including initial scan | 46.236 / 47.706 ms | 5.847 / 6.225 ms | Target passes |
| 25,000 | Initial scan | 175.454 / 215.291 ms | 179.443 / 229.326 ms | Mean +2.27%; p95 +6.52% fails |
| 25,000 | Eight requests including initial scan | 1317.599 / 1340.992 ms | 166.121 / 169.000 ms | Target passes; candidate remains rejected |

Both warm-snapshot targets also pass. Independent process peak RSS changes were
-0.44% and +0.44% at 1k and 25k respectively. These are accounting measurements,
not total thumbnail throughput or GUI responsiveness. The seven samples per
block do not establish a production p95. The first-scan tail failure is retained
without outlier removal, a looser cap or another repeat. Receipts:
`thumbnail-paired-{1000,25000}/comparison.json`, `thumbnail-paired-plan.json`,
and the original `thumbnail-{1000,25000}/comparison.json` memory receipts.

The [performance-loop skill](/Users/bradyreiner/.codex/memories/skills/photo-editor-perf-loop/SKILL.md)
requires: “Revert if the target misses badly or violates a protected guardrail.”
Its stop rules also require stopping when results are flaky or the loop budget
is exhausted. This pass stops after the final protocol rather than trying until
the near-miss tail clears the cap. The candidate integration, sampler, contracts
and probes remain reproducible under artifacts only.

## Typography: rejected

`photo_text_layout.h` repeatedly calls `percentAtLength()` while placing path
glyphs and sampling exclusion contours. Qt 6.10's path cache reduced CPU by about
40–86% on the initial contour fixtures and about 75% on complex path text. The
initial complete image, profile and layout-metric comparisons matched.

The first candidate also missed its declared metrics-only path target. That
route only needs one path length and has no repeated glyph-distance work. A
second candidate narrowed caching to the rendering path, after the metrics-only
return, while retaining contour sampling caching.

An expanded cached-versus-uncached geometry contract then found different glyph
coordinates on a four-point curve at distance 862.244. Initial matching image
hashes do not establish general placement equivalence. Both candidates remain
artifact-only, and the production typography header matches its starting hash.
No Qt version requirement or text-placement behavior changes.

## Ranked next investigations

These are current-source findings, not measured gains. Prior rounds and their
failed experiments remain relevant; this table does not reclassify them as
accepted work.

| Priority | Area and current path | Next measurement and candidate | Required guards |
| --- | --- | --- | --- |
| 1 | CPU color processing: `color_adjustments.cpp` checks the nine channel-mixer coefficients inside its pixel loop; `color_look_runtime.cpp` recomputes state-only post-color/grading/harmony eligibility per pixel. Layer color uses the scalar path too. | Attribute full preview/export look time for identity, saturation-only, graded and multi-layer recipes. Compile immutable state flags once per stage/layer; retain pixel-dependent highlight and selection calculations. Verify which compiled/scalar route actually executes. | Exact float RGBA, profiles, HDR/negative inputs, all active controls, cancellation, full export and dense-layer throughput. A cheap predicate microbenchmark alone cannot qualify a preview gain. |
| 2 | Timeline metadata inspector: `TimelineSourcePanel.qml::openMediaMetadataInspector()` calls the synchronous `inspectMediaAssetTechnicalMetadata()` before opening its popup. That method waits up to 2,500 ms for ffprobe, then up to 250 ms after killing it. | Measure click-to-popup and GUI heartbeat with fast, slow, missing and stale sources. Open the inspector immediately and populate technical data through a bounded, generation-guarded worker; cache by file revision. | Exact metadata/error fields, source replacement, stale replies, helper cleanup and inspector destruction. This finding concerns metadata inspection, not the already-investigated playback first-frame path. |
| 3 | Vector edits: `setPhotoLayerVectorPath()` converts the QVariant path through JSON, validates it, creates geometry, and converts it back through validated JSON before committing a full layer stack. | Measure 4/1,024/8,192-anchor edits through the real controller, separating conversion, geometry, history and render scheduling. Remove duplicate work using an operation-local validated document. | Exact canonical bytes, rejected malformed inputs, error/state preservation, undo/redo, normalized/document coordinates, masks and rendered pixels. `move_anchor()`'s standalone mutation costs do not prove the QML/controller cost. |
| 4 | Import planning: `importer.rs::candidate_file_paths()` collects the full walk; `plan_import_destinations()` builds RAW/JPEG pairing sets before processing candidates. It also normalizes the unchanged RAW/JPEG policy inside the file loop. | Measure time to first planned/imported batch, discovery/metadata/write time and peak memory at 1k/100k files, including nested/offline sources. Hoist policy normalization first; assess bounded discovery with pairing-aware semantics separately. | RAW/JPEG pair selection, selected-path order/deduplication, canonical destination safety, cancellation/errors and total import throughput. Streaming must preserve pairing decisions. |
| 5 | Combined cache ownership: `LibraryOriginalImageProvider` still independently retains up to three full-resolution images / 192 MiB outside the shared photo-cache owner budget. | Measure multi-workspace live ownership and critical-pressure reclamation with consumer-retained image backing. Revisit only with a fixed workload that charges subsequent redecodes and retained consumers. | Decode miss/latency, pixel metadata, copy-on-write isolation, provider lifetime, process/GPU memory and full workflow p95. Round 13's first candidate regressed peak RSS; its second had ineligible timing controls. |

For Timeline playback, the next diagnostic from round 13 remains useful: compare
one reused bare player with persistent versus replaced sinks, before another
ProRes controller-policy patch. For sustained exports, establish a long-run
memory plateau before making a whole-process memory claim.

## Verification

The release `photo_editor_app` build succeeds after restoration. The packaged
startup CTest passes (one test, 8.46 s) using a private offscreen process with
temporary data/cache/preferences directories and an unreachable private catalog
endpoint. That duration is the complete test time, not a startup latency claim.
The sampler's standalone functional contract also passes; its source and test
remain artifact-only.

The four monitored production/build-registration files match their starting
SHA256 values exactly: `catalog_asset_model.cpp`, `catalog_asset_model.h`,
`photo_text_layout.h` and `tests/CMakeLists.txt`. The candidate helper and test
were removed from production/test registration. The existing dirty work is
preserved, and the active editor was not closed or restarted by this pass.

Receipts: `restoration.json`, `decisions.json`, `restored-app-build.log`,
`restored-startup-test.log` and `thumbnail-functional.log`. The retained work is
this report and the experiment artifacts. This pass does not establish native
displayed-frame speed, whole-app throughput, a process-wide memory ceiling or
release qualification.
