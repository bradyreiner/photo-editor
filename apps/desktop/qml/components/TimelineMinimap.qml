import QtQuick

Rectangle {
    id: root
    objectName: "timelineMinimapBar"
    required property var theme
    required property var trackModel
    required property var verticalLayout
    required property real durationSeconds
    required property real horizontalOffset
    required property real horizontalZoom
    required property real viewportWidth
    required property real sequenceWidth
    required property real playheadSeconds

    readonly property real barDuration: Math.max(1, durationSeconds)
    readonly property real barScale: Math.max(0, (width - 4) / barDuration)
    property int densityBinCount: 0
    property int densityMappedTrackCount: 0
    property int densityMaximumTracksPerBin: 0
    property int densityPaintedPixelCount: 0

    signal contentXRequested(real contentX)

    readonly property string visibilityRole: "secondary|overview-scroll-playhead"
    radius: theme.radius4
    color: theme.alpha(theme.ink950, 0.78)
    border.width: 0
    border.color: "transparent"
    clip: true
    activeFocusOnTab: visible
    Accessible.role: Accessible.Slider
    Accessible.name: "Timeline overview"
    Accessible.description: "Track layout and timing overview. Drag or use Left and Right Arrow to navigate horizontally."

    Keys.onLeftPressed: contentXRequested(Math.max(
        0, horizontalOffset - viewportWidth * 0.25))
    Keys.onRightPressed: contentXRequested(Math.min(
        Math.max(0, sequenceWidth - viewportWidth),
        horizontalOffset + viewportWidth * 0.25))

    function requestedContentX(pointerX) {
        const centered = ((Number(pointerX || 0) - 2) / Math.max(0.000001, barScale))
                * horizontalZoom - viewportWidth / 2
        return Math.max(0, Math.min(Math.max(0, sequenceWidth - viewportWidth), centered))
    }

    Canvas {
        id: densityCanvas
        objectName: "timelineMinimapCanvas"
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        onTrackModelChanged: requestPaint()
        onVerticalLayoutChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onDurationSecondsChanged: requestPaint()

        readonly property var trackModel: root.trackModel || []
        readonly property var verticalLayout: root.verticalLayout || ({})
        readonly property real durationSeconds: root.barDuration

        onPaint: {
            const context = getContext("2d")
            context.reset()
            context.clearRect(0, 0, width, height)

            const tracks = trackModel || []
            const layout = verticalLayout || ({})
            const tops = layout.topByIndex || []
            const heights = layout.rowHeightByIndex || []
            const hidden = layout.hiddenByIndex || []
            const extent = Math.max(1, Number(layout.trackBottom || layout.totalHeight || 0))
            const inset = 2
            const pixelWidth = Math.max(1, Math.floor(width - inset * 2))
            const binCount = Math.max(1, Math.floor(height - inset * 2))
            const stride = pixelWidth + 1
            const visualDiff = []
            const audioDiff = []
            const trackCounts = new Int32Array(binCount)
            for (let bin = 0; bin < binCount; ++bin) {
                visualDiff.push(new Int32Array(stride))
                audioDiff.push(new Int32Array(stride))
            }

            let mappedTracks = 0
            for (let trackIndex = 0; trackIndex < tracks.length; ++trackIndex) {
                if (hidden[trackIndex] === true)
                    continue
                const track = tracks[trackIndex]
                const rowTop = Math.max(0, Number(tops[trackIndex] || 0))
                const rowHeight = Math.max(0, Number(heights[trackIndex] || 0))
                if (rowHeight <= 0)
                    continue

                const firstBin = Math.max(0, Math.min(binCount - 1,
                    Math.floor(rowTop / extent * binCount)))
                const lastBin = Math.max(firstBin, Math.min(binCount - 1,
                    Math.ceil((rowTop + rowHeight) / extent * binCount) - 1))
                for (let bin = firstBin; bin <= lastBin; ++bin)
                    trackCounts[bin] += 1
                ++mappedTracks

                const clips = track && track.clips ? track.clips : []
                const intervals = []
                for (let clipIndex = 0; clipIndex < clips.length; ++clipIndex) {
                    const clip = clips[clipIndex]
                    const rawStart = Number(clip && clip.startSeconds || 0)
                    const rawDuration = Number(clip && clip.trimmedDurationSeconds || 0)
                    if (!Number.isFinite(rawStart) || !Number.isFinite(rawDuration) || rawDuration <= 0)
                        continue
                    const start = Math.max(0, rawStart)
                    const duration = rawDuration
                    const end = Math.min(durationSeconds, start + duration)
                    if (end <= start)
                        continue
                    const left = Math.max(0, Math.min(pixelWidth - 1,
                        Math.floor(start / durationSeconds * pixelWidth)))
                    const right = Math.max(left + 1, Math.min(pixelWidth,
                        Math.ceil(end / durationSeconds * pixelWidth)))
                    intervals.push({left: left, right: right})
                }
                intervals.sort(function(a, b) { return a.left - b.left || a.right - b.right })

                const merged = []
                for (let intervalIndex = 0; intervalIndex < intervals.length; ++intervalIndex) {
                    const interval = intervals[intervalIndex]
                    const last = merged.length > 0 ? merged[merged.length - 1] : null
                    if (last && interval.left <= last.right) {
                        last.right = Math.max(last.right, interval.right)
                    } else {
                        merged.push({left: interval.left, right: interval.right})
                    }
                }

                const targetDiff = track && track.trackKind === "audio" ? audioDiff : visualDiff
                for (let bin = firstBin; bin <= lastBin; ++bin) {
                    const diff = targetDiff[bin]
                    for (let intervalIndex = 0; intervalIndex < merged.length; ++intervalIndex) {
                        const interval = merged[intervalIndex]
                        diff[interval.left] += 1
                        diff[interval.right] -= 1
                    }
                }
            }

            let maximumTracksPerBin = 0
            let paintedPixels = 0
            for (let bin = 0; bin < binCount; ++bin) {
                const rowTrackCount = trackCounts[bin]
                maximumTracksPerBin = Math.max(maximumTracksPerBin, rowTrackCount)
                if (rowTrackCount <= 0)
                    continue
                let visualActive = 0
                let audioActive = 0
                for (let pixel = 0; pixel < pixelWidth; ++pixel) {
                    visualActive += visualDiff[bin][pixel]
                    audioActive += audioDiff[bin][pixel]
                    const occupiedTracks = Math.min(rowTrackCount, visualActive + audioActive)
                    if (occupiedTracks <= 0)
                        continue
                    const density = occupiedTracks / rowTrackCount
                    const alpha = 0.07 + 0.25 * density
                    context.fillStyle = root.theme.alpha(
                        audioActive > visualActive ? root.theme.audioAccent : root.theme.primaryAccent,
                        alpha)
                    context.fillRect(inset + pixel, inset + bin, 1, 1)
                    ++paintedPixels
                }
            }

            if (root.densityBinCount !== binCount)
                root.densityBinCount = binCount
            if (root.densityMappedTrackCount !== mappedTracks)
                root.densityMappedTrackCount = mappedTracks
            if (root.densityMaximumTracksPerBin !== maximumTracksPerBin)
                root.densityMaximumTracksPerBin = maximumTracksPerBin
            if (root.densityPaintedPixelCount !== paintedPixels)
                root.densityPaintedPixelCount = paintedPixels
        }
    }

    Rectangle {
        objectName: "timelineMinimapViewport"
        x: 2 + (root.horizontalOffset / root.horizontalZoom) * root.barScale
        y: 0
        width: Math.max(20, (root.viewportWidth / root.horizontalZoom) * root.barScale)
        height: parent ? parent.height : 0
        radius: root.theme.radiusChip
        color: root.theme.alpha(root.theme.textStrong, 0.12)
        border.width: root.theme.hairlineWidth
        border.color: root.theme.alpha(root.theme.textMain, 0.45)
    }

    Rectangle {
        objectName: "timelineMinimapPlayhead"
        x: 2 + root.playheadSeconds * root.barScale
        y: 0
        width: root.theme.hairlineWidth
        height: parent ? parent.height : 0
        color: root.theme.accentWarm500
    }

    MouseArea {
        anchors.fill: parent
        onPressed: function(mouse) {
            root.forceActiveFocus(Qt.MouseFocusReason)
            root.contentXRequested(root.requestedContentX(mouse.x))
        }
        onPositionChanged: function(mouse) {
            if (pressed)
                root.contentXRequested(root.requestedContentX(mouse.x))
        }
    }
}
