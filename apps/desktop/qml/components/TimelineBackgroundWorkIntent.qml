import QtQml

QtObject {
    id: root

    property var sequenceTracks: []
    property var visibleTrackRows: []
    property var selectedClipIds: []
    property var activeProgramClipIds: []
    property string sourceClipId: ""
    property real viewportWidth: 0
    property real timelineZoom: 1
    property real minimumTimelineZoom: 0.01
    property real viewportContentX: 0
    property var clipWindow: ({
        "ready": false,
        "left": 0,
        "right": 0
    })

    // Resolve active clips once for a playhead sample. TimelineView reuses
    // these rows for its video, adjustment, and audio models rather than
    // repeating the interval-index search for every model.
    function collectActivePlaybackRows(tracks, sequenceSeconds) {
        const sourceTracks = tracks || []
        const time = Number(sequenceSeconds || 0)
        const rows = []
        let rangeSearches = 0
        let anyAudioSolo = false

        for (let trackIndex = 0; trackIndex < sourceTracks.length; ++trackIndex) {
            const track = sourceTracks[trackIndex]
            if (!track)
                continue
            if (track.trackKind === "audio" && track.solo === true)
                anyAudioSolo = true
            if (track.trackKind !== "video" && track.trackKind !== "audio")
                continue
            if (track.trackKind === "video" && track.effectiveVisible === false)
                continue
            if (track.trackKind === "audio"
                    && (track.visible === false || track.muted === true))
                continue

            const clips = track.clips || []
            const rangeIndex = track.clipRangeIndex
            const entries = rangeIndex && rangeIndex.entries ? rangeIndex.entries : []
            if (entries.length === 0)
                continue
            ++rangeSearches

            let firstIndex = 0
            let lastIndex = entries.length
            while (firstIndex < lastIndex) {
                const middleIndex = Math.floor((firstIndex + lastIndex) / 2)
                if (Number(entries[middleIndex].prefixMaximumEndSeconds) <= time)
                    firstIndex = middleIndex + 1
                else
                    lastIndex = middleIndex
            }

            let endIndex = 0
            lastIndex = entries.length
            while (endIndex < lastIndex) {
                const middleIndex = Math.floor((endIndex + lastIndex) / 2)
                if (Number(entries[middleIndex].startSeconds) <= time)
                    endIndex = middleIndex + 1
                else
                    lastIndex = middleIndex
            }

            const activeEntries = []
            for (let index = firstIndex; index < endIndex; ++index) {
                const entry = entries[index]
                if (Number(entry.endSeconds) > time)
                    activeEntries.push(entry)
            }
            if (activeEntries.length === 0)
                continue

            activeEntries.sort(function(left, right) {
                return left.clipIndex - right.clipIndex
            })
            const activeClips = []
            for (let index = 0; index < activeEntries.length; ++index) {
                const clipIndex = activeEntries[index].clipIndex
                if (clipIndex >= 0 && clipIndex < clips.length && clips[clipIndex])
                    activeClips.push(clips[clipIndex])
            }
            if (activeClips.length > 0)
                rows.push({track: track, activeClips: activeClips})
        }

        if (anyAudioSolo) {
            for (let rowIndex = rows.length - 1; rowIndex >= 0; --rowIndex) {
                const track = rows[rowIndex].track
                if (track && track.trackKind === "audio" && track.solo !== true)
                    rows.splice(rowIndex, 1)
            }
        }

        return {
            rows: rows,
            tracksVisited: sourceTracks.length,
            rangeSearches: rangeSearches,
            anyAudioSolo: anyAudioSolo
        }
    }

    // Reconcile the active-layer ListModel by stable IDs only. Keeping the
    // large clip maps out of ListModel roles avoids recursive conversion of
    // keyframe and variant arrays when QML creates a layer delegate.
    function synchronizePlaybackClipIds(model, layers) {
        const nextLayers = layers || []
        if (!model || typeof model.get !== "function"
                || typeof model.insert !== "function" || typeof model.remove !== "function"
                || typeof model.move !== "function" || typeof model.append !== "function") {
            return {ok: false, error: "Playback delegate model is unavailable."}
        }

        const nextIndexByClipId = Object.create(null)
        for (let nextIndex = 0; nextIndex < nextLayers.length; ++nextIndex) {
            const clipId = String(nextLayers[nextIndex] && nextLayers[nextIndex].clipId || "")
            if (clipId.length > 0)
                nextIndexByClipId[clipId] = nextIndex
        }

        let removed = 0
        let inserted = 0
        let moved = 0
        for (let modelIndex = model.count - 1; modelIndex >= 0; --modelIndex) {
            const existingId = String(model.get(modelIndex).clipId || "")
            if (nextIndexByClipId[existingId] === undefined) {
                model.remove(modelIndex)
                ++removed
            }
        }

        const positionByClipId = Object.create(null)
        for (let index = 0; index < model.count; ++index)
            positionByClipId[String(model.get(index).clipId || "")] = index

        for (let nextIndex = 0; nextIndex < nextLayers.length; ++nextIndex) {
            const clipId = String(nextLayers[nextIndex] && nextLayers[nextIndex].clipId || "")
            if (clipId.length === 0)
                continue
            let modelIndex = positionByClipId[clipId]
            if (modelIndex === undefined) {
                if (nextIndex >= model.count) {
                    model.append({"clipId": clipId})
                } else {
                    model.insert(nextIndex, {"clipId": clipId})
                    const positionIds = Object.keys(positionByClipId)
                    for (let positionIndex = 0; positionIndex < positionIds.length; ++positionIndex) {
                        const positionedId = positionIds[positionIndex]
                        if (positionByClipId[positionedId] >= nextIndex)
                            ++positionByClipId[positionedId]
                    }
                }
                positionByClipId[clipId] = nextIndex
                ++inserted
                continue
            }
            if (modelIndex === nextIndex)
                continue

            model.move(modelIndex, nextIndex, 1)
            const positionIds = Object.keys(positionByClipId)
            for (let positionIndex = 0; positionIndex < positionIds.length; ++positionIndex) {
                const positionedId = positionIds[positionIndex]
                const position = positionByClipId[positionedId]
                if (positionedId === clipId) {
                    positionByClipId[positionedId] = nextIndex
                } else if (modelIndex > nextIndex
                           && position >= nextIndex && position < modelIndex) {
                    positionByClipId[positionedId] = position + 1
                } else if (modelIndex < nextIndex
                           && position > modelIndex && position <= nextIndex) {
                    positionByClipId[positionedId] = position - 1
                }
            }
            ++moved
        }

        return {
            ok: true,
            inserted: inserted,
            removed: removed,
            moved: moved,
            count: model.count
        }
    }

    function horizontalVirtualizationBuffer() {
        return Math.max(320, Number(viewportWidth || 0) * 0.75)
    }

    function virtualizedClipsForTrack(track) {
        const clips = track && track.clips ? track.clips : []
        const width = Number(viewportWidth || 0)
        if (width <= 0)
            return []
        const window = clipWindow || ({})
        const zoom = Math.max(Number(minimumTimelineZoom || 0.01), Number(timelineZoom || minimumTimelineZoom || 0.01))
        const buffer = window.ready === true ? 0 : horizontalVirtualizationBuffer()
        const leftSeconds = window.ready === true
                ? Number(window.left || 0)
                : Math.max(0, (Number(viewportContentX || 0) - buffer) / zoom)
        const rightSeconds = window.ready === true
                ? Number(window.right || 0)
                : (Number(viewportContentX || 0) + width + buffer) / zoom

        // The clip list and its range index are one projection snapshot. Do
        // not pair a newly-published clips array with an older range index.
        const rangeIndex = track && track.clipRangeIndex
        if (!rangeIndex)
            return clips
        const entries = rangeIndex.entries || []
        let low = 0
        let high = entries.length
        while (low < high) {
            const middle = (low + high) >> 1
            if (Number(entries[middle].prefixMaximumEndSeconds) < leftSeconds)
                low = middle + 1
            else
                high = middle
        }
        const firstIntersectingIndex = low
        low = 0
        high = entries.length
        while (low < high) {
            const middle = (low + high) >> 1
            if (Number(entries[middle].startSeconds) <= rightSeconds)
                low = middle + 1
            else
                high = middle
        }
        const endOfVisibleStarts = low

        const visibleEntries = []
        const includedIndexes = ({})
        for (let entryIndex = firstIntersectingIndex;
             entryIndex < endOfVisibleStarts; ++entryIndex) {
            const entry = entries[entryIndex]
            if (entry && entry.clipIndex >= 0 && entry.clipIndex < clips.length
                    && clips[entry.clipIndex]
                    && Number(entry.endSeconds) >= leftSeconds) {
                visibleEntries.push(entry)
                includedIndexes[String(entry.clipIndex)] = true
            }
        }
        const selectedEntries = rangeIndex.selectedEntries || []
        for (let selectedIndex = 0; selectedIndex < selectedEntries.length; ++selectedIndex) {
            const entry = selectedEntries[selectedIndex]
            if (!entry || entry.clipIndex < 0 || entry.clipIndex >= clips.length
                    || !clips[entry.clipIndex])
                continue
            const key = String(entry.clipIndex)
            if (includedIndexes[key] !== true) {
                visibleEntries.push(entry)
                includedIndexes[key] = true
            }
        }
        visibleEntries.sort(function(left, right) {
            return left.clipIndex - right.clipIndex
        })
        const visibleClips = []
        for (let visibleIndex = 0; visibleIndex < visibleEntries.length; ++visibleIndex) {
            const clipIndex = visibleEntries[visibleIndex].clipIndex
            if (clipIndex >= 0 && clipIndex < clips.length && clips[clipIndex])
                visibleClips.push(clips[clipIndex])
        }
        return visibleClips
    }

    function collectClipIds(tracks) {
        const sourceTracks = tracks || []
        const ids = []
        const seenIds = ({})
        for (let trackIndex = 0; trackIndex < sourceTracks.length; ++trackIndex) {
            const clips = virtualizedClipsForTrack(sourceTracks[trackIndex])
            for (let clipIndex = 0; clipIndex < clips.length; ++clipIndex) {
                const clipId = String(clips[clipIndex].clipId || "")
                if (clipId.length > 0 && seenIds[clipId] !== true) {
                    seenIds[clipId] = true
                    ids.push(clipId)
                }
            }
        }
        return ids
    }

    // This is the existing production behavior: visible clip IDs are
    // collected across all document tracks, with horizontal virtualization.
    // Keep it as the measured baseline until a candidate preserves the
    // Program/source/selection priority IDs and passes its output-set oracle.
    function visibleBackgroundWorkClipIds() {
        return collectClipIds(sequenceTracks)
    }
}
