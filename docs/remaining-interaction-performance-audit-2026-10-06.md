# Remaining interaction performance issues — October 6, 2026

Read-only application audit of the current working tree. Application code and tests were not changed. The two existing benchmark targets were rebuilt; an isolated probe in `/tmp` links against the freshly rebuilt shared desktop runtime. This report and its evidence are the only repository additions from this audit.

## Highest priority findings

| Priority | Workflow and remaining cost | Fresh measurement | Source path and next change to investigate |
| --- | --- | --- | --- |
| 1 | Editing a long brush mask repeatedly normalizes existing point/sample data on the GUI thread | One 5,000-point mask: opacity callback 97.97 ms; 10,000 points: 196.27 ms. Deleting a small radial mask while retaining the brush mask: 54.59 / 108.63 ms | `setActiveMaskOpacity` → `updateMaskAt` → `applyMaskStackChange`. Normalize changed geometry once; keep scalar changes and unrelated masks on their already validated representation. |
| 2 | Timeline automatic recovery and autosave still construct whole-document JSON before dispatching worker I/O | `documentObject()` takes 40.59 ms at 1,000 clips, 203.71 ms at 5,000, and 403.40 ms at 10,000 | `submitEditJournalEntry` and the autosave timer call `documentObject()` synchronously. Capture immutable editorial values cheaply, then construct the JSON on the worker. |
| 3 | Multi-selection expands Timeline's compact projection back into full inspector records | All-selected projection: 21.96 ms at 1,000 clips, 128.29 ms at 5,000, 319.75 ms at 10,000 | `trackMap` sets `include_detail_arrays` for every selected clip; `isClipSelected` uses linear `QStringList::contains`. Keep strip selection flags separate from detailed inspector records and index selection membership. |
| 4 | Adding an empty Timeline track invalidates whole-sequence UI data and broad unrelated state | At 10,000 clips, `addVideoTrack()` is 6.93 ms without UI subscribers, 45.92 ms with just one `sequenceChanged` subscriber reading `timelineTracks()` | Track-add methods call full `emitAllState()`; `timelineTracks` enumerates every clip before QML viewport filtering. Use scoped publication and incremental/visible projections. The insertion-only notification fix does not cover empty-track creation. |
| 5 | Layer/filter deletion and accepted mask edits still reach synchronous photo recovery writes | 4 MiB raster fixture checkpoint p95: 19.09 ms for a tone-only edit, 30.68 ms for a changed matte. 16 MiB: 87.54 / 123.03 ms | `deletePhotoLayer` and `removePhotoLayerEffect` → `applyPhotoLayerStackChange` → `persistCurrentState` → `checkpointRecipeSaves`. Profile and replace whole-outbox serialization/write costs while preserving the accepted edit's immediate recovery guarantee. |

These priorities combine frequency and scope with measured costs. The masks and multi-selection findings are additional remaining cases beyond the October 5 fixes. Snapshot construction is the residual GUI-thread part of the October 6 Timeline persistence fix. Photo checkpoint writes were previously identified; they were freshly remeasured here.

## Why a scalar mask edit is expensive

`updateMaskAt()` calls `normalized_mask_definition()` for the edited mask. `applyMaskStackChange()` then calls `normalize_mask_stack()` for the complete stack. Its validation routine, `mask_definition_write_error()`, calls `normalized_mask_definition()` again. Reference repair in `preset_state_without_dangling_mask_references()` normalizes the whole stack again just to collect mask-set IDs. Brush normalization reconstructs both the flattened `brushPoints` and every stroke's `samples`.

This work runs even when only opacity changes and geometry is already normalized. The isolated fixture has no asset ID or image path, so it excludes catalog saving, real-image rendering, and asset undo retention. It verifies the actual public opacity setter reached 0.5 and that radial deletion leaves one mask. Source inspection establishes the repeated point/sample normalization; the timing covers the public callback, rather than attributing every millisecond to one substage.

## Scaling measurements

Values are medians of three independent process runs, each taking the median of five samples. All three runs completed successfully. No timing run overlapped a build or another audit workload.

| Clips | One-selected strip projection | All-selected strip projection | Empty-track add, no UI | Empty-track add, one projection subscriber | Editorial JSON snapshot |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 100 | 0.58 ms | 2.09 ms | 0.04 ms | 0.70 ms | 4.22 ms |
| 1,000 | 3.93 ms | 21.96 ms | 0.44 ms | 4.30 ms | 40.59 ms |
| 5,000 | 19.76 ms | 128.29 ms | 2.98 ms | 22.07 ms | 203.71 ms |
| 10,000 | 40.19 ms | 319.75 ms | 6.93 ms | 45.92 ms | 403.40 ms |

The Timeline fixture starts with ten tracks, evenly distributed clips, four effects per clip, and one opacity keyframe per clip. Five no-subscriber calls and five subscriber calls each add a video/audio pair, leaving 30 tracks for the all-selected and snapshot measurements. Those added tracks are empty. The one-subscriber test is a deliberately minimal synchronous model-read consumer, not the production QML shell. There is no valid media path, decoding, native scene, or durable Timeline journal in this fixture. The snapshot timer wraps the real `documentObject()` through the existing test-access adapter, with removal of its timestamp field; it does not time a worker write.

| Existing brush points | Opacity callback | Delete small radial mask, retain brush mask |
| ---: | ---: | ---: |
| 100 | 2.29 ms | 1.21 ms |
| 1,000 | 20.05 ms | 11.15 ms |
| 5,000 | 97.97 ms | 54.59 ms |
| 10,000 | 196.27 ms | 108.63 ms |

The brush fixture is normalized before timing. It contains the regular point list plus the canonical stroke sample representation generated from those points. Every measured opacity invocation changes the value. Radial-deletion samples reseed the same two-mask stack before timing the actual `removeActiveMask()` call.

## Layer and filter deletion boundaries

No new large standalone deletion cost was demonstrated for small photo recipes. The October 5 native shell evidence already reduced adjustment-deletion callbacks to about 6.37 ms by releasing hidden History bindings and deferring paint-history formatting. That remains earlier fixture evidence, not a native deletion rebaseline in this audit.

The remaining shared deletion cost is independently established by its current call chain and the freshly rebuilt checkpoint benchmark. The checkpoint fixtures contain deterministic incompressible alpha bytes, with 24 changing edits per scenario. Tone edits retain the matte; brush edits change one alpha byte. All nine scenarios passed exact reopen validation. The figures above time the checkpoint alone, not total deletion latency, helper preparation, rendering, or frame presentation. Whole-recipe preparation and helper startup also remain synchronous in `flushPersistedState()` / `start_editor_recipe_persistence()`; their current wall time was not measured here.

## Other remaining risk and proof limits

- A selected spatial grade can still invoke synchronous source-compatibility `ffprobe` on a cold cache through `currentClip()` → grade export capability → `frameStreamGradeClipIds()`. The result is cached across playback ticks now, so this is a cold-source risk, not a claim that every tick runs ffprobe. The bounded probe permits 3 seconds per uncached source; actual latency was not measured here.
- The latest stored October 5 native real-photo mask-brush gate still fails: warm feedback p95 76.55 ms against 50 ms. A separate Canvas scheduling candidate did not improve it. This audit measures additional controller work; it does not establish current native painted-frame timing.
- The ordinary Timeline projection is still about 40 ms at 10,000 clips despite the accepted compact-record improvement. Minimap and other QML consumers do additional work after projection; these were inspected but not timed.
- Timeline snapshot I/O, hidden-preview suspension, compact unselected records, History hover dwell, cached Fragment tips, and reduced warm insertion publications already have fixes. They are not being re-reported as unimplemented changes.

`timeline_history_benchmark --verify` passed. The checkpoint benchmark passed all nine exact-reopen scenarios. These are CPU/controller and persistence results, not native UI smoothness, full rendered mask correctness, decoding throughput, or a proposed patch's before/after acceptance. No speedup is claimed.

## Evidence

- [Aggregate controller measurements](evidence/remaining-interaction-performance-2026-10-06/summary.json)
- [Checkpoint p50/p95, cold writes, and reopen checks](evidence/remaining-interaction-performance-2026-10-06/checkpoint.json)
- [Isolated probe source](evidence/remaining-interaction-performance-2026-10-06/timeline_probe.cpp)
- [Source and binary hashes](evidence/remaining-interaction-performance-2026-10-06/provenance.json)
- [Verification](evidence/remaining-interaction-performance-2026-10-06/verification.json)

Raw controller reports and compile/link argument arrays are in the same evidence directory. The compile/link commands use the existing `build/release-macos` directory as their working directory. The isolated probe executable and object are retained under `/tmp/looksy-remaining-perf-audit-2026-10-06/`.
