import QtQml.Models

ListModel {
    id: root

    objectName: "timelineVirtualTrackRowsModel"
    dynamicRoles: false

    function nearestTrackIndexAtY(layout, contentY) {
        const tops = layout && layout.topByIndex ? layout.topByIndex : []
        const heights = layout && layout.rowHeightByIndex ? layout.rowHeightByIndex : []
        const hidden = layout && layout.hiddenByIndex ? layout.hiddenByIndex : []
        const y = Number(contentY)
        if (!Number.isFinite(y))
            return -1

        let nearestIndex = -1
        let nearestDistance = Number.POSITIVE_INFINITY
        for (let index = 0; index < tops.length; ++index) {
            if (hidden[index] === true)
                continue
            const top = Number(tops[index])
            const height = Number(heights[index])
            if (!Number.isFinite(top) || !Number.isFinite(height) || height <= 0)
                continue
            const bottom = top + height
            if (y >= top && y < bottom)
                return index

            const distance = y < top ? top - y : y - bottom
            if (distance < nearestDistance) {
                nearestDistance = distance
                nearestIndex = index
            }
        }
        return nearestIndex
    }

    function synchronizeRows(rows) {
        if (!Array.isArray(rows))
            return ({ok: false, error: "Track rows must be an array."})

        // Keep complex clip/keyframe maps out of the ListModel roles. Row
        // delegates resolve their full track from the owned sequence snapshot
        // using this stable ID and its absolute source index.
        const desiredRows = []
        const seenTrackIds = ({})
        for (let index = 0; index < rows.length; ++index) {
            const track = rows[index]
            const trackId = String(track && track.trackId || "")
            if (trackId.length === 0 || seenTrackIds[trackId] === true)
                return ({ok: false, error: "Visible track rows need unique stable track IDs."})
            seenTrackIds[trackId] = true
            const trackIndex = Number(track && track.trackIndex)
            if (!Number.isInteger(trackIndex) || trackIndex < 0)
                return ({ok: false, error: "Visible track rows need a nonnegative integer source index."})
            desiredRows.push({
                "trackKey": trackId,
                "trackIndex": trackIndex
            })
        }

        let structuralChanges = 0
        let updatedRows = 0
        for (let targetIndex = 0; targetIndex < desiredRows.length; ++targetIndex) {
            const desired = desiredRows[targetIndex]
            let currentIndex = targetIndex < count
                    && String(get(targetIndex).trackKey || "") === desired.trackKey
                    ? targetIndex : -1

            if (currentIndex < 0) {
                for (let searchIndex = targetIndex + 1; searchIndex < count; ++searchIndex) {
                    if (String(get(searchIndex).trackKey || "") === desired.trackKey) {
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
                        "trackKey": desired.trackKey,
                        "trackIndex": desired.trackIndex
                    })
                    ++structuralChanges
                    ++updatedRows
                    continue
                }
            }

            const current = get(currentIndex)
            if (Number(current.trackIndex) !== desired.trackIndex) {
                setProperty(currentIndex, "trackIndex", desired.trackIndex)
                ++updatedRows
            }
        }

        while (count > desiredRows.length) {
            remove(count - 1)
            ++structuralChanges
        }

        return ({
            ok: true,
            structuralChanges: structuralChanges,
            updatedRows: updatedRows,
            rowCount: count
        })
    }
}
