.pragma library

function currentClipSnapshotForSelectionRefresh(root, observedSnapshotRevision) {
    if (!root)
        return ({})

    const current = root.currentClip || ({})
    const document = root.documentController
    if (!document || typeof document.snapshotCurrentClip !== "function")
        return current

    const selectedIndex = Number(document.currentIndex)
    const cachedIndex = Number(current.index)
    const cachedClipId = String(current.clipId || "")
    const cachedPrimaryMatches = isFinite(selectedIndex)
        && selectedIndex === cachedIndex
        && (selectedIndex < 0 ? cachedClipId.length === 0 : cachedClipId.length > 0)
    const snapshotRevision = Number(root.currentClipSnapshotRevision || 0)
    const observedRevision = Number(observedSnapshotRevision || 0)

    // Authored clip edits and document reloads synchronously publish a fresh
    // current-clip snapshot before this deferred selection callback runs.
    // Reuse that exact rich map when the primary index still matches, instead
    // of asking Qt to wrap its nested keyframe/effect arrays a second time.
    // If no snapshot was published since the last selection refresh, fetch a
    // new map even at the same index so same-index content changes stay visible.
    if (cachedPrimaryMatches && snapshotRevision !== observedRevision)
        return current

    return document.snapshotCurrentClip()
}

function ownedArrayLikeSnapshot(snapshot) {
    if (Array.isArray(snapshot))
        return snapshot
    if (!snapshot || (typeof snapshot !== "object" && typeof snapshot !== "function"))
        return []

    const length = snapshot.length
    // C++ QVariantList values can cross the QML boundary as indexed wrappers
    // rather than JavaScript Arrays. Copy that wrapper once at the authored
    // refresh boundary so ListView and playback collectors see an owned array.
    // Reject values that cannot be represented as a finite, integral QML
    // array length. The native snapshot owns the actual project-sized bound.
    if (typeof length !== "number" || !isFinite(length)
            || length < 0 || Math.floor(length) !== length
            || length > 4294967295)
        return []

    const owned = []
    for (let index = 0; index < length; ++index)
        owned.push(snapshot[index])
    return owned
}

function timelineTrackSnapshot(document) {
    if (!document)
        return []
    if (typeof document.snapshotTimelineTracks !== "function") {
        return ownedArrayLikeSnapshot(document.timelineTracks)
    }
    return ownedArrayLikeSnapshot(document.snapshotTimelineTracks())
}

// TimelineDocumentController::emitAllState() emits timelineStateChanged
// immediately before sequenceChanged. A newly loaded/cleared document needs
// its track projection refreshed at the state boundary, while that paired
// sequence signal must not force a second large snapshot in the same publish.
// Editorial sequenceChanged signals that have no preceding timeline state
// still refresh normally.
function refreshSequenceTrackSnapshotForTimelineState(root) {
    if (!root || typeof root.refreshSequenceTrackSnapshot !== "function")
        return false
    const lifetime = root.timelineSelectionRefreshLifetime
    const publicationToken = Number(root.timelineStateSnapshotPublicationToken || 0) + 1
    root.timelineStateSnapshotPublicationToken = publicationToken
    root.timelineStateSnapshotRefreshPendingSequence = true
    root.refreshSequenceTrackSnapshot()
    // The controller's current publication path emits sequenceChanged
    // synchronously after timelineStateChanged. Expire the pair guard at the
    // end of this QML turn as a safety net: if that contract ever changes, a
    // later unrelated sequence edit must still fetch its own snapshot.
    Qt.callLater(function() {
        if (!lifetime || lifetime.alive !== true
                || Number(root.timelineStateSnapshotPublicationToken || 0) !== publicationToken)
            return
        root.timelineStateSnapshotRefreshPendingSequence = false
        root.timelineStateSnapshotPublicationToken = publicationToken + 1
    })
    return true
}

function refreshSequenceTrackSnapshotForSequenceChange(root) {
    if (!root || typeof root.refreshSequenceTrackSnapshot !== "function")
        return false
    if (root.timelineStateSnapshotRefreshPendingSequence === true) {
        root.timelineStateSnapshotRefreshPendingSequence = false
        root.timelineStateSnapshotPublicationToken =
                Number(root.timelineStateSnapshotPublicationToken || 0) + 1
        return false
    }
    root.refreshSequenceTrackSnapshot()
    return true
}

function selectionSignature(selectedIds) {
    if (!selectedIds || typeof selectedIds.length !== "number")
        return ""
    let signature = ""
    for (let index = 0; index < selectedIds.length; ++index)
        signature += String(selectedIds[index] || "") + "\u001f"
    return signature
}

function refreshTrackSnapshotForSelectionChange(root, selectedSignature) {
    if (!root || typeof root.refreshSequenceTrackSnapshot !== "function")
        return false
    if (String(root.sequenceTrackSnapshotSelectionSignature || "")
            === String(selectedSignature || ""))
        return false
    root.refreshSequenceTrackSnapshot()
    return true
}

function schedule(root) {
    if (!root || root.timelineSelectionRefreshPending === true)
        return
    const lifetime = root.timelineSelectionRefreshLifetime
    if (!lifetime || lifetime.alive !== true)
        return

    root.timelineSelectionRefreshPending = true
    Qt.callLater(function() {
        // The workspace loader may destroy TimelineView before the queued
        // callback runs. The token outlives its owner and is invalidated by
        // Component.onDestruction, so this branch does not touch a dead QML
        // object.
        if (lifetime.alive !== true)
            return
        root.timelineSelectionRefreshPending = false
        root.refreshTimelineSelectionState()
    })
}
