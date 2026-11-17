# Timeline selection snapshot binding review

**Date:** 2026-10-30  
**Scope:** read-only audit of the P7 save-time main-thread sample and the QML/native selected-clip snapshot path. No native project, source media, live app, MCP, or renderer was changed or used for this audit.

## Evidence and confidence

The supplied sample at `/private/tmp/looksy-polish-pass7-native-save-sample.txt` contains 249 samples of Looksy's main thread at 10 ms intervals. Every sample is in `QQmlDelayedCallQueue::executeAllExpired_Later()` and descends through recursive `QQmlBinding::update()` calls. Repeated lower frames include `QV4::Heap::VariantAssociationObject::detached()` and `QV4::Heap::Sequence::createElementWrappers()`. The stack is evidence of expensive QML binding propagation and conversion of nested `QVariantMap`/`QVariantList` values to QV4 objects during a deferred callback. It is **not** evidence that canonical document hashing is the sampled hot path.

The sample contains generated `TimelineView.qml` binding symbols, but no source line mapping identifies which one dominates. The selection callback below is a source-supported candidate for the cascade, not a proved exclusive cause. It performs a rich snapshot request and unconditionally assigns that map after selection notifications. Other full-snapshot and track-model bindings can also contribute.

The external `timeline.inspect_document` path is already bounded by `TimelineDraftLifecycle::inspectDocument()`; this report does not propose another inspect endpoint.

## Source path and repeated work

`TimelineSelectionRefresh.schedule()` coalesces `selectionChanged` into a deferred call to `root.refreshTimelineSelectionState()` (`apps/desktop/qml/components/TimelineSelectionRefresh.js`). That function requests `snapshotCurrentClip()` and assigns its result to `root.currentClip` even when the primary clip is unchanged (`TimelineView.qml`, formerly around lines 4888–4905).

There is a concrete duplicate-notification sequence in native authored edits: `publishSelectedClipState()` emits `selectionChanged`, then `selectedClipStateChanged` (`timeline_document_controller.cpp`, around line 57189). The QML `onSelectedClipStateChanged` handler synchronously requests and assigns a selected-clip snapshot (around line 5069), while `onSelectionChanged` schedules another full snapshot for the next event-loop turn. Full timeline-state publication similarly refreshes document snapshots synchronously before a deferred selection callback can run.

`snapshotCurrentClip()` delegates to `clipMap(current_index_)` (`timeline_document_controller.cpp`, around line 7027). The full clip map enables detail arrays and effect-stack data by default. Depending on the selected clip it contains full keyframe projections, effect stack and detail arrays, color-grade structures, motion-tracking maps, mask maps, and generator parameters. Keyframe maps are built by `insertClipKeyframeProjection()` (around line 44323), effect details are inserted in the full map around line 43511, and motion tracking is inserted around line 43057. P7 has a real serialized clip with **1,204 keyframes**: clip `9854c417-eb5d-46b6-94fa-9935e7ec53d0` on track index 44, clip index 10.

The root `currentClip` map is passed to multiple consumers, including the audio mixer and effects panel. The detached curve window reads `availableKeyframeParameters`, `keyframes`, and effect data. This is legitimate feature data and should remain available. Separately, `timelineTracks()` uses a compact projection, but selected clips still carry their complete keyframe projection; it may add work during sequence refresh and is not isolated by this sample.

## Experiment: deep JSON detachment

I ran a standalone Qt 6.10.2/QML comparison at `/private/tmp/looksy_clip_snapshot_benchmark.cpp`, using the real P7 clip's serialized keyframes/vector path plus representative nested effect, mask, and grade fields. Eight QML bindings read the nested structures. Across five measured repetitions of 80 updates each, the median was:

| Update path | Median for 80 updates |
|---|---:|
| Assign the QVariantMap directly | 207 ms |
| `JSON.parse(JSON.stringify(map))` before assignment | 1,075 ms |

The JSON round-trip preserved the asserted clip ID, all 1,204 keyframes, effect name, nested effect mask, color-grade curves, and clip mask. It was about **5.2× slower** in this dense-map fixture, so deep JSON cloning is not a suitable performance patch. This isolated harness is a comparative experiment, not an application benchmark or proof of native UI speed.

## Narrow QML change now staged for focused review

The current working-tree change leaves the full clip map intact and avoids a deep clone. It tracks when a new rich snapshot has been adopted. In the deferred selection refresh, it reuses the same QML object only if:

1. a synchronous snapshot has arrived since the previous deferred selection refresh;
2. that cached map's `index` still matches `documentController.currentIndex`; and
3. the cached map's clip ID state matches the selected/empty state.

Otherwise it still calls native `snapshotCurrentClip()`, including same-index refreshes when no newer snapshot was published. Snapshot adoption is identity-guarded so it does not reassign the already-current object. The synchronous authored-state and timeline-state snapshot routes remain in place. This targets the duplicate deferred map conversion while preserving the same nested keyframe/effect/grade/mask payload and native setter routes.

The focused selection-refresh CTest passed: `ctest --test-dir build/dev-macos-debug --output-on-failure -R '^timeline_selection_refresh_coalescing_test$'` (1/1, 0.73 s). It covers a same-index fetch when no fresh synchronous snapshot is observed; reuse after a synchronous snapshot; a later same-index content update; primary index change; clear/reselect; and continued access to nested keyframe, effect, mask, and color-grade fields. This is focused QML/controller-helper proof. No native UI interaction, app presentation sample, or P7 render has been run against this QML change.

## Files in scope

- `apps/desktop/qml/components/TimelineSelectionRefresh.js`
- `apps/desktop/qml/workspaces/TimelineView.qml`
- Standalone comparison: `/private/tmp/looksy_clip_snapshot_benchmark.cpp`
