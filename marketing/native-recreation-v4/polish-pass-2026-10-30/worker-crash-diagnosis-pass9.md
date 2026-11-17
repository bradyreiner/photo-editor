# Native worker crash diagnosis, pass 9

**Date:** 2026-10-30 21:08 local  
**Scope:** offline crash and source review plus a compact QML track-snapshot fix. No worker was relaunched, and no native project or source media was mutated.

## Crash evidence

Crash report: `/Users/bradyreiner/Library/Logs/DiagnosticReports/Looksy-2026-10-30-210829.ips`  
Runtime log: `/private/tmp/looksy-polish-exact-clock-selection-native-runtime.log`

PID 36110 crashed on the main thread with `EXC_BAD_ACCESS/SIGSEGV` at `0xffffffffffffffff`. The top frames are:

```text
QQmlData::disconnectNotifiers(QQmlData::DeleteNotifyList)
QQmlData::markAsDeleted(QObject*)
QQmlPrivate::QQmlElement<QQuickColumn>::~QQmlElement()
QObject::event(QEvent*)
QCoreApplicationPrivate::sendPostedEvents(...)
QCocoaEventDispatcherPrivate::processPostedEvents()
```

This identifies a failure while Qt is disconnecting QML property notifiers during posted destruction of a `QQuickColumn`. The report does not identify which QML component owns that column, so it is not enough to attribute the invalid notifier to a particular `Repeater`, `Loader`, or callback.

The runtime log records a timeline frame summary at `2026-10-31T03:08:18.886Z` (21:08:18.886 local) with `max=39687ms`, about four seconds before the crash. This is evidence of a severe main-thread stall near the failure, not proof that the stall corrupted the notifier.

The earlier PID 82448 report is a distinct crash: its top frames are `QV4::Object::insertMember` and `setElementFallback`, faulting at `0xa0`. Both reports are in Qt/QML on the main thread, but their stacks do not establish a shared cause.

## Source path and bounded fix

`TimelineDocumentController::emitAllState()` invalidates the compact track projection and emits `sequenceChanged` during authored clip updates. Before this fix, `TimelineView.qml` bound `sequenceTrackModel` directly to the `timelineTracks` `QVariantList` property. The existing `snapshotSequenceTracks()` comment documents that iterating a `QVariantList` property from QML bindings can repeatedly re-enter the whole-document getter and wrap nested `QVariantMap`/`QVariantList` values.

The source change adds `snapshotTimelineTracks()`, which returns the existing compact projection through a `Q_INVOKABLE` boundary, and routes TimelineView through one owned snapshot at initialization and once per `sequenceChanged`. A stored selected-ID signature handles `selectClip(index)`, which emits `selectionChanged` without `sequenceChanged`: the deferred selection refresh fetches once if the signature differs, and skips a second fetch when a preceding sequence refresh already captured that selection. Stable track IDs and the existing virtualized row model remain in place. It does not change render quality, timeline content, or the grade path.

This targets the observed projection/wrapping churn and is a plausible contributor to the long stall. It is **not yet evidence that the fix prevents the `QQuickColumn` notifier crash**; that requires a protected native replay against the new binary.

## Regression coverage

The existing `timeline_sequence_track_snapshot_workflow_test` checks native compact projection parity after repeated title-animation updates, verifies `selectClip(index)` emits no `sequenceChanged` while the compact snapshot still reports fresh single-selection flags, and checks multi-selection flags. Its QML fixture covers 16 sequence notifications with a nested 256-keyframe payload, selection-only single/multi updates, and deduplication when the sequence snapshot already contains the same selection. It asserts preservation of effect, mask, grade, keyframe, stable-ID, and range-index data. The direct native QML boundary is checked with `Array.isArray`. The existing `timeline_selection_refresh_coalescing_test` remains a separate focused gate.

`git diff --check` passed. The combined Debug build completed, and `timeline_sequence_track_snapshot_workflow_test` and `timeline_selection_refresh_coalescing_test` both passed (0.83 s and 0.13 s). This is focused controller/QML test evidence, not a native worker replay or a crash-prevention qualification.
