# Further interaction performance findings — October 6, 2026

Read-only follow-up to the [completed interaction fixes](interaction-performance-fixes-2026-10-06.md). This audit changed no application code or tests. It adds this report and isolated probe evidence. Measurements below are CPU/controller costs; they do not establish native frame latency or a proposed optimization's speedup.

## Additional findings

1. **Mask UI presentation still rebuilds unchanged brush geometry.** The public scalar edit callback is fast after the earlier fix, but `maskSets()` rebuilds both grouped selection and retouch data after each `invalidateMaskDerivedCaches()`. `groupMaskSets()` normalizes every mask twice, and `groupRetouchStack()` normalizes selection masks before rejecting their role. The real opacity setter followed by the UI getter takes **59.46 ms with 10,000 brush points**, or **30.09 ms at 5,000**. Geometry was normalized before timing. `controlsChanged` publishes these getters, including to `MaskInspectorSections.qml:20` and `:1088`; `StudioWorkspace.qml:14995` also binds a mask-set Repeater inside a hidden strip. This is a remaining presentation cost, distinct from the fixed callback normalizer. Reuse validated geometry in presentation and check role before reconstructing a full definition.

   Source: `editor_preview_controller.cpp:19279`, `:43757`, `:44077`; `thumbnail_presentation_service.cpp:144`, `:374`. The separate `reusableMaskOutputs()` getter spends another **14.87 ms** normalizing that same brush even though the fixture has no reusable outputs. The inspector calls it twice during construction (`MaskInspectorSections.qml:1520`, `:1537`); no claim is made that this invokable reruns on every slider event.

2. **Gap queries scale quadratically, and navigation repeats the query.** `timelineGaps()` checks every clip for sequence-wide coverage and downstream affected counts for each gap. Listing 9,999 gaps in a 10,000-clip single-track fixture takes **1,271.73 ms**; 4,999 gaps take **325.50 ms**. The actual `navigateTimelineGap()` command builds the list and then calls `previewTimelineGap()`, which builds it again: **2,831.02 ms at 10,000 clips**, **678.55 ms at 5,000**. This is synchronous work behind Gap Index opening, duration-filter Apply and next/previous navigation. `closeAllTimelineGaps()` also queries twice before its shifting work; full close-all latency was not measured. Index sorted intervals and downstream counts, and reuse one query result during navigation while preserving locked-track and linked-group checks.

   Source: `timeline_document_controller.cpp:44004`, `:44078`, `:44167`, `:44223`, `:44242`, `:44352`; UI `TimelineView.qml:3997`, `:4030`, `:11136`.

3. **The Pancake editor retains an expensive full destination projection after closing.** `sequenceTracks()` costs **129.57 ms at 10,000 clips**, compared with **39.42 ms** for the compact `timelineTracks()` getter in the same fixture. At 5,000 clips these are **64.61 / 19.45 ms**. `TimelinePancakeEditor.qml:480` binds the heavier getter. Its popup Loader is active when `contentCreated || visible`, and sets `contentCreated` permanently true on first load. Thus closing it retains the model binding and later `sequenceChanged` publications remain eligible to rebuild it. The retained binding is verified from source; the cost of a closed production popup was not measured. Give this view a compact destination model and suspend expensive model bindings while closed, retaining draft interaction state as needed.

   Source: `timeline_document_controller.h:34`, `timeline_document_controller.cpp:4948`, `:43672`; popup lifetime `TimelineView.qml:15452`.

4. **Bulk lift and extract still use repeated linear selection searches.** The earlier set lookup fix applies to Timeline projections. `liftSelectedClips()` and `extractSelectedClips()` still call `QStringList::contains` inside whole-clip traversals. At 10,000 selected clips, actual remove/lift takes **57.61 ms**, and actual extract/ripple deletion takes **174.40 ms**, before production UI subscribers or disk saving. Every sample checks that removal succeeds and undo restores the original clip count. Extract's additional range and ripple work is included, so the entire time is not attributed solely to membership searches. Build one selection set per command and measure range-shifting separately.

   Source: `timeline_document_controller.cpp:44374`, `:44403`, `:44519`; Extract UI command `TimelineView.qml:6115`.

5. **Transcript models rebuild every word on broad clip notifications and also survive popup closure.** At 5,000 caption segments / 50,000 words, one `transcriptSegments()` read takes **18.25 ms**, and one cleanup-suggestion read **9.74 ms**. There are multiple count, model and empty-state consumers. Both properties notify on `clipsChanged`, so unrelated clip operations can refresh them. The transcript Loader also stays active after first opening. This creates repeated allocations even with its popup closed; the production total per edit has not been measured. Cache transcript models by relevant caption/analysis revision and suspend hidden consumers. The cleanup fixture exercises sorting and word normalization; suggestion-heavy output was not qualified.

   Source: `timeline_document_controller.h:41`, `:42`; `.cpp:5207`, `:5274`; `TimelineView.qml:12585`, `:12633`, `:12790`, `:12887`, `:13641`.

6. **Media-bin counts scan the assignment table once per bin.** `mediaBins()` takes **20.58 ms with 500 bins / 10,000 asset assignments**, versus **5.50 ms with 100 bins** and **0.84 ms with 10 bins**. It also scans all bins for child counts. Switching the active bin emits `mediaBinsChanged` despite unchanged counts, and full editorial publication still emits it for many operations. Cache or preaggregate counts and separate active selection from structural/count notifications. This is a lower-priority large-project cost.

   Source: `timeline_document_controller.cpp:10518`, `:10590`, `:43695`; `TimelineSourcePanel.qml:217`, `:3229`, `:3395`.

## Scaling and evidence

Three independent processes per workload family; each value is the median of process medians. Getters use five samples per process, gap queries and actual remove/extract/navigation commands use three. Audit builds and probe workloads did not overlap timing samples. Ranges and raw reports are retained.

| Clips | Gap list (ms) | Pancake projection (ms) | Remove all (ms) | Next Gap command (ms) | Extract all (ms) |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 100 | 0.34 | 1.35 | 0.03 | 0.76 | 0.05 |
| 1000 | 15.07 | 12.84 | 0.70 | 31.09 | 1.96 |
| 5000 | 325.49 | 64.61 | 13.40 | 678.55 | 40.21 |
| 10000 | 1271.73 | 129.57 | 57.61 | 2831.02 | 174.40 |

| Existing normalized brush points | Opacity edit + mask UI getter (ms) | Reusable-output getter (ms) |
| ---: | ---: | ---: |
| 100 | 0.91 | 0.18 |
| 1000 | 6.42 | 1.51 |
| 5000 | 30.09 | 7.48 |
| 10000 | 59.46 | 14.87 |

Timeline projection/removal fixtures use ten tracks, four effects and one opacity keyframe per clip. Gap/navigation fixtures use one track with two-second trimmed clips starting every four seconds, yielding exactly one two-second gap between adjacent clips. These are deliberately large stress fixtures. There is no valid media path or durable Timeline destination, and no production QML subscribers. The brush fixture has one canonical selection brush with flattened points and corresponding stroke samples, but no real asset or persistence destination. Transcript fixtures contain ten timed words per caption; bin fixtures use flat bins.

The isolated probes compile with release `-O3` and link a frozen copy of the last successful October 6 desktop test runtime. An attempted fresh target rebuild failed in unrelated Carousel clipboard code; concurrent work subsequently corrected those source errors. No present build failure is asserted here. The complete Timeline controller source/header and MaskSession match the hashes recorded for the successful earlier build. Audited mask getter/invalidation bodies match the frozen source and were unchanged by the earlier fix patch; the current Editor header adds only a nonvirtual member function. This establishes the inspected paths and ABI used by these probes; it does not establish a fresh packaged-app result. Full source/runtime/probe hashes and the failed build-attempt log are retained.

All six probe processes exited successfully. Fixture checks cover model counts, real opacity mutations, next-gap results, and successful remove/extract undo. There is no exported-image, native frame, real-photo or decoding-throughput claim. The previously reported native brush-frame gate, whole-strip empty-track publication, changed-matte recovery write, explicit Timeline JSON save and cold source probe risks remain separate open findings.

[Measurements and ranges](evidence/further-interaction-performance-2026-10-06/summary.json), [probe source](evidence/further-interaction-performance-2026-10-06/probe.cpp), [source/runtime provenance](evidence/further-interaction-performance-2026-10-06/provenance.json), and [fixture checks and proof limits](evidence/further-interaction-performance-2026-10-06/verification.json). Frozen runtime and executable remain in `/tmp/looksy-more-performance-audit-2026-10-06/`.
