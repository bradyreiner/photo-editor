import QtQml

QtObject {
    // A header represents a lane. Inspect its selected clip, then its topmost
    // clip under the playhead, then the nearest clip. Never move or seek media.
    function clipForTrack(tracks, trackId, playheadSeconds, selectedClipId) {
        const rows = tracks || []
        let track = null
        for (let i = 0; i < rows.length; ++i) {
            if (String(rows[i].trackId || "") === String(trackId)) {
                track = rows[i]
                break
            }
        }
        if (!track)
            return null
        const clips = track.clips || []
        const time = Number(playheadSeconds || 0)
        let nearest = null
        let nearestDistance = Number.POSITIVE_INFINITY
        let active = null
        for (let i = 0; i < clips.length; ++i) {
            const clip = clips[i]
            if (String(clip.clipId || "") === String(selectedClipId || "")
                    && String(selectedClipId || "").length > 0)
                return clip
            const start = Number(clip.startSeconds || 0)
            const end = clip.endSeconds !== undefined ? Number(clip.endSeconds)
                    : start + Number(clip.trimmedDurationSeconds || 0)
            if (end > start && time >= start && time < end)
                active = clip
            const distance = time < start ? start - time : Math.max(0, time - end)
            if (distance < nearestDistance) {
                nearestDistance = distance
                nearest = clip
            }
        }
        return active || nearest
    }

    function initialTab(clip) {
        if (Number(clip && clip.effectCount || 0) > 0)
            return "effects"
        if (Number(clip && clip.keyframeCount || 0) > 0
                || (clip && clip.keyframes && clip.keyframes.length > 0))
            return "animate"
        const type = String(clip && clip.mediaType || "")
        if (type === "title" || type === "caption")
            return "title"
        return type === "audio" ? "audio" : "motion"
    }
}
