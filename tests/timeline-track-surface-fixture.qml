import QtQuick
import QtQuick.Controls.Basic
import "../apps/desktop/qml/components" as Components

ApplicationWindow {
    id: fixture
    width: 720
    height: 350
    visible: true
    color: "#17191c"

    property string lastRenamedTrackName: ""
    property string activeTool: "select"
    property string lastPaletteTool: ""
    property string lastPaletteCreateKind: ""
    property int trackSelectionDirection: 1
    property bool hasActiveTimeline: true
    property bool rateStretchEnabled: true
    property bool envelopePenEnabled: true
    property var headerTrackData: ({
        trackId: "video-1",
        trackIndex: 0,
        trackKind: "video",
        trackRole: "media",
        displayLabel: "V1 · Interview",
        customName: "Interview",
        visible: true,
        effectiveVisible: true,
        muted: false,
        solo: false,
        locked: false,
        targeted: false,
        laneIndex: 0,
        clipCount: 0
    })
    property var audioTrackData: ({
        trackId: "audio-1",
        trackIndex: 1,
        trackKind: "audio",
        trackRole: "media",
        displayLabel: "A1 · Dialogue",
        customName: "Dialogue",
        visible: true,
        effectiveVisible: true,
        muted: false,
        solo: false,
        locked: false,
        targeted: false,
        laneIndex: 0,
        isIndependent: true,
        clipCount: 0
    })

    Components.Theme { id: sharedTheme }

    QtObject {
        id: fakeTimelineDocument
        objectName: "fakeTimelineDocument"
        property var trackGroups: []
        property int videoTrackCount: 1
        property int framesPerSecond: 30
        property real sequencePlayheadSeconds: 0
        property int lastRenamedTrackIndex: -1
        property int inspectRequestCount: 0
        property string lastInspectedTrackId: ""

        function inspectTrackRow(trackId) {
            inspectRequestCount += 1
            lastInspectedTrackId = String(trackId || "")
        }

        function renameTrackRow(index, customName) {
            lastRenamedTrackIndex = index
            fixture.lastRenamedTrackName = String(customName || "")
            var updated = ({})
            for (var key in fixture.headerTrackData)
                updated[key] = fixture.headerTrackData[key]
            updated.customName = String(customName || "")
            updated.displayLabel = "V1 · " + updated.customName
            fixture.headerTrackData = updated
        }

        function toggleTrackVisibility() {}
        function toggleTrackMuted() {}
        function toggleTrackSolo() {}
        function toggleTrackTargeted() {}
        function toggleTrackLocked() {}
        function moveTrackRow() {}
        function removeTrackRow() {}
        function duplicateTrackRow() {}
        function setTrackGroupCollapsed() {}
        function setTrackGroup() {}
        function setTrackGroupParent() {}
        function setTrackGroupState() {}
        function selectTrackGroupClips() {}
        function renameTrackGroup() {}
        function createTrackGroup() { return true }
        function setTrackGroupTransform() {}
        function beginContinuousEdit() {}
        function endContinuousEdit() {}
        function toggleTrackAutomationKeyframe() {}
        function clearTrackAutomation() {}
        function setTrackAutomationValue() {}
        function setTrackAudioSetting() {}
    }

    Components.TimelineTrackHeader {
        id: testedTrackHeader
        objectName: "testedTimelineTrackHeader"
        x: 12
        y: 18
        width: 168
        height: 44
        theme: sharedTheme
        trackData: fixture.headerTrackData
        timelineDocument: fakeTimelineDocument
        trackOrderData: ({
            groupDepth: 0,
            pairedLaneIndex: 0,
            standaloneVisualPosition: -1,
            standaloneVisualCount: 0,
            independentAudioPosition: -1,
            independentAudioCount: 0,
            isGroupHead: false
        })
        onInspectRequested: fakeTimelineDocument.inspectTrackRow(trackData.trackId)
    }

    Components.TimelineTrackHeader {
        id: testedAudioTrackHeader
        objectName: "testedAudioTimelineTrackHeader"
        x: 12
        y: 66
        width: 168
        height: 36
        theme: sharedTheme
        trackData: fixture.audioTrackData
        timelineDocument: fakeTimelineDocument
        trackOrderData: ({
            groupDepth: 0,
            pairedLaneIndex: 0,
            standaloneVisualPosition: -1,
            standaloneVisualCount: 0,
            independentAudioPosition: 0,
            independentAudioCount: 1,
            isGroupHead: false
        })
        onInspectRequested: fakeTimelineDocument.inspectTrackRow(trackData.trackId)
    }

    Components.TimelineClipContent {
        id: testedClipContent
        objectName: "testedTimelineClipContent"
        x: 370
        y: 18
        width: 320
        height: 40
        title: "A Very Long Interview Clip Name That Must Stay Readable"
        metadata: "00:00:03:18 · 4K camera original"
        mediaType: "video"
        statusBadges: []
        horizontalInset: 24
        rightInset: 24
        selected: true
    }

    Components.TimelineClipContent {
        id: testedTextClipLabel
        objectName: "testedTimelineTextClipLabel"
        visible: false
        width: 260
        height: 44
        title: "Light spills across the doorway"
        mediaType: "title"
        labelOnly: true
        clipData: ({
            mediaType: "title",
            timelineLabel: "Light spills across the doorway"
        })
    }

    Components.TimelineClipContent {
        id: testedShapeClipLabel
        objectName: "testedTimelineShapeClipLabel"
        visible: false
        width: 260
        height: 44
        title: "Title"
        mediaType: "title"
        labelOnly: true
        clipData: ({
            mediaType: "title",
            timelineLabel: "Title",
            timelineTypeSummary: "Title shape · ellipse"
        })
    }

    Components.TimelineClipContent {
        id: testedPhotoClipLabel
        objectName: "testedTimelinePhotoClipLabel"
        visible: false
        width: 260
        height: 44
        title: "IMG_8492"
        mediaType: "photo"
        labelOnly: true
        editSummary: "Masked · Film Grain · Animated"
        clipData: ({
            mediaType: "photo",
            timelineLabel: "IMG_8492",
            filePath: "/media/Harbor_Lights.JPG",
            keyframeCount: 4,
            timelineAnimationSummary: "Scale · Position",
            effectNames: ["Film Grain"],
            timelineMaskSummary: "Clip mask · ellipse"
        })
    }

    Components.TimelineClipContent {
        id: testedGeneratorClipLabel
        objectName: "testedTimelineGeneratorClipLabel"
        visible: false
        width: 260
        height: 44
        title: "Animated"
        mediaType: "motion-generator"
        labelOnly: true
        editSummary: "Animated"
        clipData: ({
            mediaType: "motion-generator",
            timelineLabel: "Animated",
            keyframeCount: 8,
            timelineTypeSummary: "Vector path · 12 points"
        })
    }

    Components.TimelineToolPalette {
        id: testedTimelineToolPalette
        x: 12
        y: 116
        width: fixture.width - 24
        height: implicitHeight
        theme: sharedTheme
        columns: 5
        // This fixture exercises the labeled variant. The default compact icon
        // palette is covered by timeline_tool_palette_ui_test.
        showLabels: true
        showCreationTools: true
        activeTool: fixture.activeTool
        hasActiveTimeline: fixture.hasActiveTimeline
        rateStretchEnabled: fixture.rateStretchEnabled
        rateStretchDisabledReason: "Select an unlocked clip without a retime curve."
        envelopePenEnabled: fixture.envelopePenEnabled
        envelopePenDisabledReason: "Select an unlocked clip or track envelope."
        trackSelectionDirection: fixture.trackSelectionDirection
        onTrackSelectionDirectionChanged:
            fixture.trackSelectionDirection = trackSelectionDirection
        onToolRequested: function(tool) {
            fixture.lastPaletteTool = tool
            fixture.activeTool = tool
        }
        onCreateRequested: function(kind) {
            fixture.lastPaletteCreateKind = kind
        }
    }
}
