import QtQml
import QtQml.Models

ListModel {
    id: root

    objectName: "timelineVirtualClipRowsModel"
    dynamicRoles: false

    // Keep full clip maps, including keyframe arrays, in the owning timeline
    // snapshot. This model contains only stable identity and a source index.
    property var sourceClips: []
    property var visibleClips: []
    property bool suspendUpdates: false
    property bool componentsReady: false
    property int synchronizationCount: 0

    property Timer synchronizationTimer: Timer {
        interval: 0
        repeat: false
        onTriggered: root.synchronize()
    }

    Component.onCompleted: {
        componentsReady = true
        scheduleSynchronization()
    }

    onSourceClipsChanged: scheduleSynchronization()
    onVisibleClipsChanged: scheduleSynchronization()
    onSuspendUpdatesChanged: {
        if (!suspendUpdates)
            scheduleSynchronization()
    }

    function scheduleSynchronization() {
        if (!componentsReady || suspendUpdates)
            return false
        synchronizationTimer.restart()
        return true
    }

    function synchronize() {
        if (!componentsReady)
            return ({"ok": false, "error": "Clip rows are not ready."})
        synchronizationTimer.stop()
        if (suspendUpdates)
            return ({"ok": true, "suspended": true, "structuralChanges": 0,
                     "updatedRows": 0, "rowCount": count})

        const clips = sourceClips || []
        const visible = visibleClips || []
        const sourceCount = Number(clips.length)
        const visibleCount = Number(visible.length)
        if (!Number.isInteger(sourceCount) || sourceCount < 0 ||
                !Number.isInteger(visibleCount) || visibleCount < 0)
            return ({"ok": false, "error": "Clip snapshots must be indexed array-like values."})

        // Build the index from the current source snapshot once per sync. A
        // visible clip can move after insert/reorder, while its stable ID and
        // delegate remain unchanged.
        const sourceIndexByClipId = Object.create(null)
        for (let sourceIndex = 0; sourceIndex < sourceCount; ++sourceIndex) {
            const clip = clips[sourceIndex]
            const clipId = String(clip && clip.clipId || "")
            if (clipId.length === 0 || sourceIndexByClipId[clipId] !== undefined)
                return ({"ok": false,
                         "error": "Source clips need unique, nonempty stable IDs."})
            sourceIndexByClipId[clipId] = sourceIndex
        }

        const desiredRows = []
        const seenClipIds = Object.create(null)
        for (let visibleIndex = 0; visibleIndex < visibleCount; ++visibleIndex) {
            const clip = visible[visibleIndex]
            const clipId = String(clip && clip.clipId || "")
            const sourceIndex = sourceIndexByClipId[clipId]
            if (clipId.length === 0 || sourceIndex === undefined || seenClipIds[clipId] === true)
                return ({"ok": false,
                         "error": "Visible clips need unique IDs from the current source snapshot."})
            seenClipIds[clipId] = true
            desiredRows.push({clipKey: clipId, sourceClipIndex: sourceIndex})
        }

        let structuralChanges = 0
        let updatedRows = 0
        for (let targetIndex = 0; targetIndex < desiredRows.length; ++targetIndex) {
            const desired = desiredRows[targetIndex]
            let currentIndex = targetIndex < count
                    && String(get(targetIndex).clipKey || "") === desired.clipKey
                    ? targetIndex : -1

            if (currentIndex < 0) {
                for (let searchIndex = targetIndex + 1; searchIndex < count; ++searchIndex) {
                    if (String(get(searchIndex).clipKey || "") === desired.clipKey) {
                        currentIndex = searchIndex
                        break
                    }
                }
                if (currentIndex >= 0) {
                    move(currentIndex, targetIndex, 1)
                    ++structuralChanges
                    currentIndex = targetIndex
                } else {
                    insert(targetIndex, {
                        "clipKey": desired.clipKey,
                        "sourceClipIndex": desired.sourceClipIndex
                    })
                    ++structuralChanges
                    ++updatedRows
                    continue
                }
            }

            const current = get(currentIndex)
            if (Number(current.sourceClipIndex) !== desired.sourceClipIndex) {
                setProperty(currentIndex, "sourceClipIndex", desired.sourceClipIndex)
                ++updatedRows
            }
        }

        while (count > desiredRows.length) {
            remove(count - 1)
            ++structuralChanges
        }

        ++synchronizationCount
        return ({
            "ok": true,
            "suspended": false,
            "structuralChanges": structuralChanges,
            "updatedRows": updatedRows,
            "rowCount": count
        })
    }
}
