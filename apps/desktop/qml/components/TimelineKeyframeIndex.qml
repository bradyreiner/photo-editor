import QtQml

QtObject {
    id: root

    // Keep a bounded set of immutable snapshots. Qt's QVariantMap wrappers are
    // not safe WeakMap keys across QV4 garbage collections; retaining ordinary
    // references also lets active playback reuse the exact channel objects.
    property var snapshotChannels: []
    // Dense carousel and layered motion scenes can contain over 100 animated
    // snapshots. Keep their working set without unbounded historical retention.
    readonly property int maximumSnapshots: 128
    readonly property var emptyChannels: Object.create(null)

    function channelsFor(snapshot, source) {
        if (!snapshot || typeof snapshot !== "object")
            return emptyChannels
        // Static clips and unanimated tracks need no retained channel index.
        // Otherwise their empty entries evict animated layers in dense scenes,
        // forcing every channel to be copied and sorted again on each frame.
        if (!source || source.length === 0)
            return emptyChannels
        for (let index = snapshotChannels.length - 1; index >= 0; --index) {
            const cached = snapshotChannels[index]
            if (cached.snapshot === snapshot)
                return cached.channels
        }
        const channels = Object.create(null)
        const points = source || []
        for (let index = 0; index < points.length; ++index) {
            const point = Object.assign({}, points[index])
            const parameter = String(point.parameter || "")
            if (!channels[parameter])
                channels[parameter] = []
            point.seconds = Number(point.seconds || 0)
            point.value = Number(point.value || 0)
            channels[parameter].push(point)
        }
        const parameters = Object.keys(channels)
        for (let index = 0; index < parameters.length; ++index)
            channels[parameters[index]].sort(function(left, right) {
                return left.seconds - right.seconds
            })
        snapshotChannels.push({snapshot: snapshot, channels: channels})
        if (snapshotChannels.length > maximumSnapshots)
            snapshotChannels.shift()
        return channels
    }

    function clipParameter(clip, parameter) {
        return channelsFor(clip, clip ? clip.keyframes : null)[String(parameter)] || []
    }

    function trackParameter(track, override, parameter) {
        return channelsFor(override || track,
                           override || (track ? track.automationKeyframes : null))[String(parameter)] || []
    }

    // The first right endpoint at or after the requested time preserves the
    // existing hold/equal-time semantics while avoiding a linear scan per frame.
    function rightEndpoint(points, seconds) {
        let low = 1
        let high = points.length - 1
        while (low < high) {
            const middle = Math.floor((low + high) / 2)
            if (points[middle].seconds < seconds)
                low = middle + 1
            else
                high = middle
        }
        return low
    }
}
