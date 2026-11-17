import QtQuick
import "../apps/desktop/qml/components" as Components

Item {
    id: root
    objectName: "timelineOverviewTestHarness"
    width: 420
    height: 260

    property real horizontalOffset: 50
    property real horizontalZoom: 20
    property real sequenceDuration: 30
    property real sequenceWidth: 900
    property real viewportWidth: 324
    property real fitDurationSeconds: 298
    property real twoHourDurationSeconds: 7200
    property real yearDurationSeconds: 31536000
    property real longDurationSeconds: 3153600000000
    readonly property real fitMinimumZoom: scaleGeometry.minimumZoomForRange(
                                               viewportWidth - 40, fitDurationSeconds,
                                               scaleGeometry.minimumZoom)
    readonly property real fitZoom: scaleGeometry.fitZoom(
                                       viewportWidth - 40, fitDurationSeconds,
                                       fitMinimumZoom, 600)
    readonly property real yearFitMinimumZoom: scaleGeometry.minimumZoomForRange(
                                                  viewportWidth - 40, yearDurationSeconds,
                                                  scaleGeometry.minimumZoom)
    readonly property real yearFitZoom: scaleGeometry.fitZoom(
                                            viewportWidth - 40, yearDurationSeconds,
                                            yearFitMinimumZoom, 600)
    readonly property real longFitMinimumZoom: scaleGeometry.minimumZoomForRange(
                                                  viewportWidth - 40, longDurationSeconds,
                                                  scaleGeometry.minimumZoom)
    readonly property real longFitZoom: scaleGeometry.fitZoom(
                                           viewportWidth - 40, longDurationSeconds,
                                           longFitMinimumZoom, 600)
    readonly property real twoHourFitZoom: scaleGeometry.fitZoom(
                                              viewportWidth - 40, twoHourDurationSeconds,
                                              scaleGeometry.minimumZoomForRange(
                                                  viewportWidth - 40, twoHourDurationSeconds,
                                                  scaleGeometry.minimumZoom), 600)
    readonly property bool lowZoomStepIsApplied: scaleGeometry.zoomChangedEnough(
                                                    twoHourFitZoom,
                                                    twoHourFitZoom * 1.01)
    readonly property bool lowZoomNoiseIsIgnored: scaleGeometry.zoomChangedEnough(
                                                     twoHourFitZoom,
                                                     twoHourFitZoom + 0.000000001)
    readonly property real oneFrameDurationSeconds: 1 / 30
    readonly property real firstShortClipWidth: scaleGeometry.clipWidth(
                                                    oneFrameDurationSeconds, fitZoom)
    readonly property real firstShortClipStartX: 40
    readonly property real secondShortClipStartX: firstShortClipStartX + firstShortClipWidth
    readonly property real secondShortClipWidth: scaleGeometry.clipWidth(
                                                     oneFrameDurationSeconds, fitZoom)
    readonly property real firstShortClipHitWidth: scaleGeometry.hitAreaWidth(
                                                      firstShortClipWidth, 1)
    readonly property real firstShortClipEndSeconds: oneFrameDurationSeconds
    readonly property bool shortClipOwnsItsStart: scaleGeometry.containsSequenceTime(
                                                     0, 0, oneFrameDurationSeconds)
    readonly property bool shortClipOwnsItsEnd: scaleGeometry.containsSequenceTime(
                                                   oneFrameDurationSeconds, 0,
                                                   oneFrameDurationSeconds)
    readonly property real hitFixtureZoom: 22.5
    readonly property real hitFixtureClipWidth: scaleGeometry.clipWidth(
                                                   oneFrameDurationSeconds, hitFixtureZoom)
    property string clipHitAreaSelection: ""
    property int clipHitAreaRejections: 0
    property real requestedSeekSeconds: -1
    property real rulerPointerContentX: -1
    property var tracks: []
    property var verticalLayout: ({})
    property var tickModel: [
        {seconds: 0, isMajor: true, label: "00:00"},
        {seconds: 5, isMajor: true, label: "00:05"},
        {seconds: 12.5, isMajor: false, label: ""},
        {seconds: 15, isMajor: true, label: "00:15"}
    ]

    function rulerViewportXForSeconds(seconds) {
        return ruler.viewportXForSeconds(seconds)
    }

    function rulerSequenceSecondsAt(viewportX) {
        return ruler.sequenceSecondsAt(viewportX)
    }

    Components.Theme { id: theme }
    Components.TimelineScaleGeometry { id: scaleGeometry }
    Components.TimelineBackgroundWorkIntent { id: timelineBackgroundWorkIntent }
    ListModel {
        id: playbackDelegateModel
        dynamicRoles: true
    }
    ListModel {
        id: playbackPayloadHeavyMapModel
        dynamicRoles: true
    }
    ListModel {
        id: playbackPayloadIdsOnlyModel
        dynamicRoles: true
    }
    Repeater {
        model: playbackDelegateModel
        delegate: Item {
            required property string clipId
            objectName: "playbackDelegate-" + clipId
            Component.onCompleted: root.playbackSyncDelegateCreated += 1
            Component.onDestruction: root.playbackSyncDelegateDestroyed += 1
        }
    }

    property int playbackSnapshotTracksVisited: 0
    property int playbackSnapshotInputClipCount: 0
    property int playbackSnapshotRangeSearches: 0
    property int playbackSnapshotActiveTrackCount: 0
    property int playbackSnapshotActiveClipCount: 0
    property bool playbackSnapshotDetectedAudioSolo: false
    property string playbackSnapshotActiveIds: ""
    property int playbackSyncInitialInsertCount: 0
    property int playbackSyncStableInsertCount: 0
    property int playbackSyncStableMoveCount: 0
    property int playbackSyncStableRemoveCount: 0
    property int playbackSyncReorderMoveCount: 0
    property int playbackSyncFinalInsertCount: 0
    property int playbackSyncFinalRemoveCount: 0
    property int playbackSyncDelegateCreated: 0
    property int playbackSyncDelegateDestroyed: 0
    property bool playbackSyncOmitsClipMapRole: false
    property int playbackPayloadClipCount: 0
    property int playbackPayloadKeyframeCount: 72
    property int playbackPayloadHeavyMapMs: 0
    property int playbackPayloadIdsOnlyMs: 0
    property bool playbackPayloadHeavyRolePresent: false
    property bool playbackPayloadIdsOnlyRoleOmitted: false

    function probeLargePlaybackSnapshot() {
        const sourceTracks = []
        let nextClip = 0
        for (let trackIndex = 0; trackIndex < 940; ++trackIndex) {
            const trackKind = trackIndex === 1 ? "audio" : "video"
            const clips = []
            function addClip(startSeconds, endSeconds, mediaType) {
                clips.push({
                    clipId: "playback-clip-" + nextClip++,
                    index: clips.length,
                    startSeconds: startSeconds,
                    endSeconds: endSeconds,
                    mediaType: mediaType,
                    sourceUrl: mediaType === "audio" ? "file:///fixture.wav" : ""
                })
            }
            addClip(0, 2, trackKind === "audio" ? "audio" : "graphic")
            if (trackIndex < 270)
                addClip(5 + trackIndex / 1000, 5.5 + trackIndex / 1000, "graphic")
            if (trackIndex % 100 === 0)
                addClip(49, 51, "graphic")
            if (trackIndex === 0)
                addClip(49.25, 50.75, "graphic")
            if (trackIndex === 1)
                addClip(49, 51, "audio")

            const entries = []
            let prefixMaximumEndSeconds = -Infinity
            const clipOrder = []
            for (let clipIndex = 0; clipIndex < clips.length; ++clipIndex)
                clipOrder.push(clipIndex)
            clipOrder.sort(function(left, right) {
                const delta = clips[left].startSeconds - clips[right].startSeconds
                return delta !== 0 ? delta : left - right
            })
            for (let orderIndex = 0; orderIndex < clipOrder.length; ++orderIndex) {
                const clipIndex = clipOrder[orderIndex]
                const clip = clips[clipIndex]
                prefixMaximumEndSeconds = Math.max(prefixMaximumEndSeconds, clip.endSeconds)
                entries.push({
                    clipIndex: clipIndex,
                    startSeconds: clip.startSeconds,
                    endSeconds: clip.endSeconds,
                    prefixMaximumEndSeconds: prefixMaximumEndSeconds
                })
            }
            sourceTracks.push({
                trackId: "playback-track-" + trackIndex,
                trackIndex: trackIndex,
                trackKind: trackKind,
                effectiveVisible: true,
                visible: true,
                muted: false,
                solo: trackIndex === 1,
                clips: clips,
                clipRangeIndex: {entries: entries}
            })
        }

        const snapshot = timelineBackgroundWorkIntent.collectActivePlaybackRows(sourceTracks, 50)
        const activeIds = []
        const rows = snapshot.rows || []
        let activeClipCount = 0
        for (let rowIndex = 0; rowIndex < rows.length; ++rowIndex) {
            const rowClips = rows[rowIndex].activeClips || []
            for (let clipIndex = 0; clipIndex < rowClips.length; ++clipIndex) {
                activeIds.push(String(rowClips[clipIndex].clipId || ""))
                ++activeClipCount
            }
        }
        playbackSnapshotTracksVisited = Number(snapshot.tracksVisited || 0)
        playbackSnapshotInputClipCount = nextClip
        playbackSnapshotRangeSearches = Number(snapshot.rangeSearches || 0)
        playbackSnapshotActiveTrackCount = rows.length
        playbackSnapshotActiveClipCount = activeClipCount
        playbackSnapshotDetectedAudioSolo = snapshot.anyAudioSolo === true
        playbackSnapshotActiveIds = activeIds.join(",")
    }

    function probePlaybackDelegateSynchronization() {
        function layer(clipId, revision) {
            return {
                clipId: clipId,
                keyframes: [{parameter: "frameOffsetX", value: revision}],
                outputVariantOverrides: [{keyframes: [{value: revision}]}]
            }
        }
        const initial = timelineBackgroundWorkIntent.synchronizePlaybackClipIds(
                    playbackDelegateModel, [layer("playback-a", 0), layer("playback-b", 0)])
        const stable = timelineBackgroundWorkIntent.synchronizePlaybackClipIds(
                    playbackDelegateModel, [layer("playback-a", 1), layer("playback-b", 1)])
        const reorder = timelineBackgroundWorkIntent.synchronizePlaybackClipIds(
                    playbackDelegateModel, [layer("playback-b", 2), layer("playback-a", 2)])
        const finalSync = timelineBackgroundWorkIntent.synchronizePlaybackClipIds(
                    playbackDelegateModel, [layer("playback-a", 3), layer("playback-c", 3)])
        playbackSyncInitialInsertCount = Number(initial.inserted || 0)
        playbackSyncStableInsertCount = Number(stable.inserted || 0)
        playbackSyncStableMoveCount = Number(stable.moved || 0)
        playbackSyncStableRemoveCount = Number(stable.removed || 0)
        playbackSyncReorderMoveCount = Number(reorder.moved || 0)
        playbackSyncFinalInsertCount = Number(finalSync.inserted || 0)
        playbackSyncFinalRemoveCount = Number(finalSync.removed || 0)
        playbackSyncOmitsClipMapRole = playbackDelegateModel.count === 2
                && playbackDelegateModel.get(0).clipData === undefined
                && playbackDelegateModel.get(1).keyframes === undefined
    }

    function benchmarkPlaybackListPayloads() {
        const layers = []
        const keyframeCount = playbackPayloadKeyframeCount
        for (let layerIndex = 0; layerIndex < 583; ++layerIndex) {
            const keyframes = []
            const variantKeyframes = []
            for (let keyframeIndex = 0; keyframeIndex < keyframeCount; ++keyframeIndex) {
                const value = keyframeIndex / keyframeCount
                keyframes.push({
                    parameter: "frameOffsetX",
                    localSeconds: value,
                    value: value * 0.25,
                    interpolation: "linear"
                })
                if (keyframeIndex < 36) {
                    variantKeyframes.push({
                        parameter: "frameOffsetY",
                        localSeconds: value,
                        value: value * -0.25,
                        interpolation: "linear"
                    })
                }
            }
            layers.push({
                clipId: "payload-layer-" + layerIndex,
                clipData: {
                    clipId: "payload-layer-" + layerIndex,
                    keyframes: keyframes,
                    outputVariantOverrides: [{variantId: "active", keyframes: variantKeyframes}]
                }
            })
        }
        playbackPayloadClipCount = layers.length

        playbackPayloadHeavyMapModel.clear()
        let startedAt = Date.now()
        for (let index = 0; index < layers.length; ++index) {
            playbackPayloadHeavyMapModel.insert(index, {
                clipId: layers[index].clipId,
                clipData: layers[index].clipData
            })
        }
        playbackPayloadHeavyMapMs = Date.now() - startedAt
        playbackPayloadHeavyRolePresent = playbackPayloadHeavyMapModel.count === layers.length
                && playbackPayloadHeavyMapModel.get(0).clipData !== undefined

        playbackPayloadIdsOnlyModel.clear()
        startedAt = Date.now()
        const result = timelineBackgroundWorkIntent.synchronizePlaybackClipIds(
                    playbackPayloadIdsOnlyModel, layers)
        playbackPayloadIdsOnlyMs = Date.now() - startedAt
        playbackPayloadIdsOnlyRoleOmitted = result.ok === true
                && playbackPayloadIdsOnlyModel.count === layers.length
                && playbackPayloadIdsOnlyModel.get(0).clipData === undefined
    }

    Component.onCompleted: {
        const rows = []
        const tops = []
        const heights = []
        const hidden = []
        let y = 0
        for (let index = 0; index < 936; ++index) {
            const rowHeight = 32
            rows.push({
                trackId: "track-" + index,
                trackKind: index % 8 === 0 ? "audio" : "video",
                clips: [{startSeconds: 20, trimmedDurationSeconds: 5}]
            })
            tops.push(y)
            heights.push(rowHeight)
            hidden.push(false)
            y += rowHeight + 2
        }
        tracks = rows
        verticalLayout = ({
            topByIndex: tops,
            rowHeightByIndex: heights,
            hiddenByIndex: hidden,
            trackBottom: y,
            totalHeight: y
        })
        probeLargePlaybackSnapshot()
        probePlaybackDelegateSynchronization()
        benchmarkPlaybackListPayloads()
    }

    Column {
        anchors.fill: parent
        spacing: 0

        Row {
            width: parent.width
            height: 24
            spacing: 0
            Rectangle {
                objectName: "rulerLaneHeaderGutter"
                width: 96
                height: parent.height
                color: theme.ink900
            }
            Components.TimelineRuler {
                id: ruler
                objectName: "timelineRuler"
                width: parent.width - 96
                height: parent.height
                theme: theme
                tickModel: root.tickModel
                cacheRangeModel: []
                horizontalOffset: root.horizontalOffset
                horizontalZoom: root.horizontalZoom
                sequenceDuration: root.sequenceDuration
                sequenceWidth: root.sequenceWidth
                playheadSeconds: 12.5
                onPointerPressed: function(x, y, button, modifiers) {
                    if (button !== Qt.LeftButton)
                        return
                    root.rulerPointerContentX = horizontalOffset + x
                    root.requestedSeekSeconds = sequenceSecondsAt(x)
                }
            }
        }

        Row {
            width: parent.width
            height: 180
            spacing: 0
            Rectangle {
                objectName: "trackLaneHeaderGutter"
                width: 96
                height: parent.height
                color: theme.ink900
            }
            Flickable {
                id: trackViewport
                objectName: "trackViewport"
                width: parent.width - 96
                height: parent.height
                contentWidth: root.sequenceWidth
                contentHeight: 936 * 34
                clip: true
                interactive: false
                Item { width: parent.contentWidth; height: parent.contentHeight }
            }
        }

        Components.TimelineMinimap {
            id: minimap
            objectName: "timelineMinimapBar"
            width: parent.width
            height: 24
            theme: theme
            trackModel: root.tracks
            verticalLayout: root.verticalLayout
            durationSeconds: root.sequenceDuration
            horizontalOffset: root.horizontalOffset
            horizontalZoom: root.horizontalZoom
            viewportWidth: root.viewportWidth
            sequenceWidth: root.sequenceWidth
            playheadSeconds: 0
            onContentXRequested: function(contentX) { root.horizontalOffset = contentX }
        }
    }

    Item {
        objectName: "subpixelClipHitFixture"
        x: 0
        y: 230
        width: 100
        height: 24

        Rectangle {
            objectName: "secondSubpixelClipBody"
            x: 50 + root.hitFixtureClipWidth
            width: root.hitFixtureClipWidth
            height: 20
            color: "#72845b"

            Components.TimelineClipPointerArea {
                objectName: "secondSubpixelClipHitArea"
                bodyWidth: parent.width
                minimumHitWidth: 1
                contentXOrigin: parent.x
                sequenceStartSeconds: parent.x / root.hitFixtureZoom
                sequenceDurationSeconds: root.oneFrameDurationSeconds
                pixelsPerSecond: root.hitFixtureZoom
                acceptedButtons: Qt.LeftButton
                onSequencePressed: root.clipHitAreaSelection = "second"
                onSequenceHitRejected: root.clipHitAreaRejections += 1
            }
        }

        Rectangle {
            objectName: "firstSubpixelClipBody"
            x: 50
            width: root.hitFixtureClipWidth
            height: 20
            color: "#536f88"

            Components.TimelineClipPointerArea {
                objectName: "firstSubpixelClipHitArea"
                bodyWidth: parent.width
                minimumHitWidth: 1
                contentXOrigin: parent.x
                sequenceStartSeconds: parent.x / root.hitFixtureZoom
                sequenceDurationSeconds: root.oneFrameDurationSeconds
                pixelsPerSecond: root.hitFixtureZoom
                acceptedButtons: Qt.LeftButton
                onSequencePressed: root.clipHitAreaSelection = "first"
                onSequenceHitRejected: root.clipHitAreaRejections += 1
            }
        }
    }
}
