import QtQuick

QtObject {
    // View-only filtering. Stable document row indices, ordering and clips never
    // change. Half-open ranges exclude a clip ending exactly at the left edge.
    function collect(tracks, startSeconds, endSeconds, selectedClipIds, groupHierarchy) {
        const visible = ({})
        const activeGroups = ({})
        const selected = ({})
        const rows = tracks || []
        const ids = selectedClipIds || []
        const insertionLaneByKind = ({})
        // Keep the newest empty destination of each kind. Retaining every
        // abandoned empty lane can bury a large project's working clips above
        // the fold. Explicitly targeted lanes are retained independently.
        for (let row = 0; row < rows.length; ++row) {
            const track = rows[row]
            if (track && (track.clips || []).length === 0)
                insertionLaneByKind[String(track.trackKind || track.kind || "video")] = row
        }
        for (let i = 0; i < ids.length; ++i)
            selected[String(ids[i])] = true
        for (let row = 0; row < rows.length; ++row) {
            const track = rows[row]
            if (!track)
                continue
            const clips = track.clips || []
            const kind = String(track.trackKind || track.kind || "video")
            let include = track.targeted === true
                    || (clips.length === 0 && insertionLaneByKind[kind] === row)
            for (let i = 0; i < clips.length && !include; ++i) {
                const clip = clips[i]
                const start = Number(clip.startSeconds || 0)
                const end = clip.endSeconds !== undefined ? Number(clip.endSeconds)
                        : start + Number(clip.trimmedDurationSeconds || 0)
                include = selected[String(clip.clipId || "")] === true
                        || (end > startSeconds && start < endSeconds && end > start)
            }
            if (include) {
                visible[String(track.trackId || "")] = true
                if (String(track.groupId || "").length > 0)
                    activeGroups[String(track.groupId)] = true
            }
        }
        // Retain group heads, including parent heads, so collapse/expand remains
        // available even when that particular lane has no clip in the range.
        const groupById = ({})
        for (let row = 0; row < rows.length; ++row) {
            const track = rows[row]
            const groupId = String(track && track.groupId || "")
            if (groupId.length > 0 && !groupById[groupId])
                groupById[groupId] = track
        }
        const pending = Object.keys(activeGroups)
        for (let i = 0; i < pending.length; ++i) {
            const head = groupById[pending[i]]
            if (!head)
                continue
            visible[String(head.trackId || "")] = true
            const group = (groupHierarchy || ({}))[pending[i]]
            const parentId = String(group ? group.parentGroupId || "" : head.groupParentId || "")
            if (parentId.length > 0 && activeGroups[parentId] !== true) {
                activeGroups[parentId] = true
                pending.push(parentId)
            }
        }
        return {"trackIds": visible, "count": Object.keys(visible).length}
    }
}
