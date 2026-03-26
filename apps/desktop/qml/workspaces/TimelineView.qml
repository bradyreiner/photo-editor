import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import "../components"

Rectangle {
    id: root
    color: "transparent"

    Theme { id: theme }

    property var currentClip: timelineDocument.currentClip
    property real playheadSeconds: timelineDocument.currentClipPlayheadSeconds
    property real sequencePlayheadSeconds: timelineDocument.sequencePlayheadSeconds
    property bool sequencePlaying: timelineDocument.sequencePlaying
    property real timelineZoom: 84
    property real minimumClipWidth: 48
    property real snapThresholdPixels: 10
    property bool snappingEnabled: true
    property bool rippleModeEnabled: true
    property real sourceDurationSeconds: 0
    property real sourceTrimInSeconds: 0
    property real sourceTrimOutSeconds: 0
    readonly property var sequenceTrackModel: timelineDocument.sequenceTracks ? timelineDocument.sequenceTracks : []
    readonly property real laneHeaderWidth: 96
    readonly property real laneHeight: 56
    readonly property real rulerHeight: 28
    readonly property real trackSpacing: 6
    readonly property real timelineContentWidth: Math.max(
                                                    timelineViewport.width,
                                                    Math.max(1, sequenceDurationSeconds()) * timelineZoom + 140)
    readonly property real timelineContentHeight: rulerHeight
                                                  + Math.max(1, sequenceTrackModel.length) * (laneHeight + trackSpacing)

    function clipSourceUrl() {
        return timelineDocument.playbackSourceUrl ? timelineDocument.playbackSourceUrl : ""
    }

    function trimStartSeconds() {
        return Number(currentClip.trimStartSeconds || 0)
    }

    function trimEndSeconds() {
        return Number(currentClip.trimEndSeconds || 0)
    }

    function hasPlayableClip() {
        return clipSourceUrl().length > 0
    }

    function shouldShowVideoMonitor() {
        return hasPlayableClip() && (sequencePlaying || timelinePlayback.mediaReady)
    }

    function shouldShowTimelinePoster() {
        return timelineDocument.previewSource.length > 0 && (!hasPlayableClip() || !timelinePlayback.mediaReady)
    }

    function hasLibraryVideoSelection() {
        return catalogAssets.selectedMediaType === "video"
                && catalogAssets.selectedFilePath.length > 0
    }

    function resetSourceTrimRange() {
        if (!hasLibraryVideoSelection()) {
            sourceDurationSeconds = 0
            sourceTrimInSeconds = 0
            sourceTrimOutSeconds = 0
            return
        }

        sourceDurationSeconds = timelineDocument.sourceDurationSecondsForFile(catalogAssets.selectedFilePath)
        sourceTrimInSeconds = 0
        sourceTrimOutSeconds = sourceDurationSeconds
    }

    function libraryPreviewSource() {
        return catalogAssets.selectedThumbnailSource ? catalogAssets.selectedThumbnailSource : ""
    }

    function sequenceDurationSeconds() {
        return Number(timelineDocument.totalDurationSeconds || 0)
    }

    function formatSeconds(seconds) {
        const safe = Math.max(0, Number(seconds || 0))
        const total = Math.floor(safe)
        const hours = Math.floor(total / 3600)
        const minutes = Math.floor((total % 3600) / 60)
        const remaining = total % 60
        if (hours > 0)
            return hours + ":" + String(minutes).padStart(2, "0") + ":" + String(remaining).padStart(2, "0")
        return minutes + ":" + String(remaining).padStart(2, "0")
    }

    function timecodeText(seconds) {
        const safe = Math.max(0, Number(seconds || 0))
        const fps = Math.max(1, Math.round(Number(timelineDocument.framesPerSecond || 30)))
        const totalFrames = Math.floor(safe * fps)
        const hours = Math.floor(totalFrames / (3600 * fps))
        const minutes = Math.floor((totalFrames % (3600 * fps)) / (60 * fps))
        const remainingSeconds = Math.floor((totalFrames % (60 * fps)) / fps)
        const frames = totalFrames % fps
        return String(hours).padStart(2, "0")
                + ":" + String(minutes).padStart(2, "0")
                + ":" + String(remainingSeconds).padStart(2, "0")
                + ":" + String(frames).padStart(2, "0")
    }

    function clipWidth(durationSeconds) {
        return Math.max(minimumClipWidth, Number(durationSeconds || 0) * timelineZoom)
    }

    function snapSequenceSeconds(proposedSeconds, excludedClipId) {
        if (!snappingEnabled)
            return Math.max(0, Number(proposedSeconds || 0))

        const thresholdSeconds = snapThresholdPixels / Math.max(1, timelineZoom)
        let snappedSeconds = Math.max(0, Number(proposedSeconds || 0))
        let nearestDistance = thresholdSeconds + 1
        const snapPoints = [sequencePlayheadSeconds]

        for (let trackIndex = 0; trackIndex < sequenceTrackModel.length; ++trackIndex) {
            const track = sequenceTrackModel[trackIndex]
            if (!track || track.trackKind !== "video" || !track.clips)
                continue
            for (let clipIndex = 0; clipIndex < track.clips.length; ++clipIndex) {
                const clip = track.clips[clipIndex]
                if (!clip || clip.clipId === excludedClipId)
                    continue
                snapPoints.push(Number(clip.startSeconds || 0))
                snapPoints.push(Number(clip.endSeconds || 0))
            }
        }

        for (let index = 0; index < snapPoints.length; ++index) {
            const candidate = Math.max(0, Number(snapPoints[index] || 0))
            const distance = Math.abs(candidate - snappedSeconds)
            if (distance <= thresholdSeconds && distance < nearestDistance) {
                nearestDistance = distance
                snappedSeconds = candidate
            }
        }

        return snappedSeconds
    }

    function seekTimelineFromX(xPos) {
        timelinePlayback.pause()
        timelinePlayback.seekSequenceSeconds(Math.max(0, Math.min(sequenceDurationSeconds(), xPos / timelineZoom)))
    }

    function splitAtPlayhead() {
        if (!hasPlayableClip())
            return

        timelinePlayback.pause()

        if (timelineDocument.splitSelectedClip(playheadSeconds))
            timelineDocument.seekCurrentClipSeconds(trimStartSeconds())
    }

    function programVideoFillMode() {
        switch (String(currentClip.frameMode || "fit")) {
        case "fill":
            return VideoOutput.PreserveAspectCrop
        case "stretch":
            return VideoOutput.Stretch
        default:
            return VideoOutput.PreserveAspectFit
        }
    }

    function programImageFillMode() {
        switch (String(currentClip.frameMode || "fit")) {
        case "fill":
            return Image.PreserveAspectCrop
        case "stretch":
            return Image.Stretch
        default:
            return Image.PreserveAspectFit
        }
    }

    function appendSelectedSourceClip() {
        if (!hasLibraryVideoSelection())
            return
        timelineDocument.addClipFromGallery(
                    catalogAssets.selectedAssetId,
                    catalogAssets.selectedFilePath,
                    catalogAssets.selectedCameraLabel,
                    sourceDurationSeconds,
                    catalogAssets.selectedThumbnailSource)
    }

    function insertSelectedSourceClip() {
        if (!hasLibraryVideoSelection())
            return
        timelineDocument.insertClipFromGallery(
                    catalogAssets.selectedAssetId,
                    catalogAssets.selectedFilePath,
                    catalogAssets.selectedCameraLabel,
                    sourceDurationSeconds,
                    sourceTrimInSeconds,
                    sourceTrimOutSeconds,
                    catalogAssets.selectedThumbnailSource)
    }

    function overwriteSelectedSourceClip() {
        if (!hasLibraryVideoSelection())
            return
        timelineDocument.overwriteClipFromGallery(
                    catalogAssets.selectedAssetId,
                    catalogAssets.selectedFilePath,
                    catalogAssets.selectedCameraLabel,
                    sourceDurationSeconds,
                    sourceTrimInSeconds,
                    sourceTrimOutSeconds,
                    catalogAssets.selectedThumbnailSource)
    }

    Component.onCompleted: {
        resetSourceTrimRange()
        timelinePlayback.attachVideoOutput(videoMonitor)
    }
    Component.onDestruction: timelinePlayback.attachVideoOutput(null)

    Connections {
        target: catalogAssets

        function onSelectedAssetChanged() {
            root.resetSourceTrimRange()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                text: "Timeline Editor"
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: 26
                font.weight: Font.DemiBold
            }

            Rectangle {
                width: 1
                height: 18
                color: theme.lineSoft
            }

            Label {
                text: timelineDocument.clipCount + " clips  •  " + timelineDocument.sequenceFormatLabel
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }

            ActionButton {
                text: "New Timeline"
                onClicked: timelineDocument.createTimeline(timelineNameField.text)
            }

            ActionButton {
                text: "Save Timeline"
                tone: "primary"
                enabled: timelineDocument.clipCount > 0
                onClicked: {
                    if (timelineDocument.saveTimeline())
                        catalogAssets.refresh()
                }
            }

            ActionButton {
                text: "Add Track"
                onClicked: timelineDocument.addVideoTrack()
            }
        }

        Label {
            Layout.fillWidth: true
            text: timelineDocument.status
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 12
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            TextField {
                id: timelineNameField
                Layout.fillWidth: true
                text: timelineDocument.timelineName
                color: theme.textStrong
                font.family: theme.fontUi
                selectByMouse: true
                placeholderText: "Timeline name"
                onEditingFinished: timelineDocument.timelineName = text

                background: Rectangle {
                    radius: 12
                    color: theme.ink850
                    border.width: 1
                    border.color: theme.lineSoft
                }
            }

            InfoPill {
                label: "Tracks"
                value: timelineDocument.videoTrackCount + "V / " + timelineDocument.audioTrackCount + "A"
            }

            InfoPill {
                label: "Sequence"
                value: formatSeconds(sequencePlayheadSeconds) + " / " + formatSeconds(sequenceDurationSeconds())
            }

            InfoPill {
                label: "Clip"
                value: currentClip.trimmedDurationText && currentClip.trimmedDurationText.length > 0
                       ? currentClip.trimmedDurationText
                       : "No clip"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 248
                Layout.fillHeight: true
                radius: 18
                color: theme.alpha(theme.ink900, 0.82)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.55)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Label {
                        text: "Source"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }

                    Label {
                        Layout.fillWidth: true
                        text: catalogAssets.selectedAssetId.length > 0
                              ? catalogAssets.selectedCameraLabel
                              : "Select a video in Library"
                        color: theme.textMain
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: catalogAssets.selectedDurationText.length > 0
                              ? catalogAssets.selectedDurationText
                              : "Drag media in from Library or add the current selection."
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 14
                        color: theme.alpha(theme.ink950, 0.72)
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.42)
                        implicitHeight: 226

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8

                            Label {
                                text: "Source Monitor"
                                color: theme.textStrong
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 110
                                radius: 10
                                color: "#0b1014"
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    source: root.libraryPreviewSource()
                                    fillMode: Image.PreserveAspectFit
                                    visible: source.length > 0
                                    cache: false
                                    asynchronous: false
                                }

                                Label {
                                    anchors.centerIn: parent
                                    visible: root.libraryPreviewSource().length === 0
                                    text: "Select a Library video"
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                }
                            }

                            Label {
                                text: "In  •  " + Number(sourceTrimInSeconds || 0).toFixed(2) + "s"
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Slider {
                                Layout.fillWidth: true
                                from: 0
                                to: Math.max(0.05, sourceDurationSeconds)
                                value: sourceTrimInSeconds
                                enabled: root.hasLibraryVideoSelection() && sourceDurationSeconds > 0
                                onMoved: sourceTrimInSeconds = Math.min(value, Math.max(0, sourceTrimOutSeconds - 0.05))
                            }

                            Label {
                                text: "Out  •  " + Number(sourceTrimOutSeconds || 0).toFixed(2) + "s"
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Slider {
                                Layout.fillWidth: true
                                from: 0
                                to: Math.max(0.05, sourceDurationSeconds)
                                value: sourceTrimOutSeconds
                                enabled: root.hasLibraryVideoSelection() && sourceDurationSeconds > 0
                                onMoved: sourceTrimOutSeconds = Math.max(value, Math.min(sourceDurationSeconds, sourceTrimInSeconds + 0.05))
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Append"
                            enabled: catalogAssets.selectedMediaType === "video"
                            onClicked: root.appendSelectedSourceClip()
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Insert"
                            tone: "primary"
                            enabled: catalogAssets.selectedMediaType === "video"
                            onClicked: root.insertSelectedSourceClip()
                        }
                    }

                    ActionButton {
                        Layout.fillWidth: true
                        text: "Overwrite"
                        enabled: catalogAssets.selectedMediaType === "video"
                        onClicked: root.overwriteSelectedSourceClip()
                    }

                    ActionButton {
                        Layout.fillWidth: true
                        text: "Load Selected Timeline"
                        enabled: catalogAssets.selectedMediaType === "timeline"
                                 && catalogAssets.selectedTimelineDocumentPath.length > 0
                        onClicked: timelineDocument.loadTimeline(catalogAssets.selectedTimelineDocumentPath)
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 14
                        color: theme.alpha(theme.ink950, 0.72)
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.42)
                        implicitHeight: 150

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8

                            Label {
                                text: "Sequence Readout"
                                color: theme.textStrong
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Label {
                                text: timecodeText(sequencePlayheadSeconds)
                                color: theme.accentWarm300
                                font.family: theme.fontMono
                                font.pixelSize: 18
                            }

                            Label {
                                text: timelineDocument.sequenceFormatLabel
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                                wrapMode: Text.Wrap
                            }

                            Label {
                                text: timelineDocument.savedRenderPath.length > 0 ? "Saved to gallery" : "Unsaved timeline"
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 18
                color: theme.alpha(theme.ink900, 0.82)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.55)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 18
                        color: "#0b1014"
                        clip: true

                        VideoOutput {
                            id: videoMonitor
                            anchors.centerIn: parent
                            width: parent.width - 24
                            height: parent.height - 24
                            fillMode: root.programVideoFillMode()
                            visible: root.shouldShowVideoMonitor()
                            transformOrigin: Item.Center
                            scale: currentClip.frameScale ? currentClip.frameScale : 1
                            anchors.horizontalCenterOffset: (currentClip.frameOffsetX ? currentClip.frameOffsetX : 0) * width * 0.18
                            anchors.verticalCenterOffset: (currentClip.frameOffsetY ? currentClip.frameOffsetY : 0) * height * 0.18
                        }

                        Image {
                            anchors.centerIn: parent
                            width: parent.width - 24
                            height: parent.height - 24
                            source: timelineDocument.previewSource
                            fillMode: root.programImageFillMode()
                            cache: false
                            asynchronous: false
                            visible: root.shouldShowTimelinePoster()
                            transformOrigin: Item.Center
                            scale: currentClip.frameScale ? currentClip.frameScale : 1
                            anchors.horizontalCenterOffset: (currentClip.frameOffsetX ? currentClip.frameOffsetX : 0) * width * 0.18
                            anchors.verticalCenterOffset: (currentClip.frameOffsetY ? currentClip.frameOffsetY : 0) * height * 0.18
                        }

                        Image {
                            anchors.centerIn: parent
                            width: parent.width - 24
                            height: parent.height - 24
                            source: root.libraryPreviewSource()
                            fillMode: Image.PreserveAspectFit
                            cache: false
                            asynchronous: false
                            visible: !root.hasPlayableClip()
                                     && !sequencePlaying
                                     && timelineDocument.previewSource.length === 0
                                     && root.hasLibraryVideoSelection()
                                     && source.length > 0
                        }

                        Rectangle {
                            visible: !root.hasPlayableClip()
                                     && timelineDocument.previewSource.length === 0
                                     && !root.hasLibraryVideoSelection()
                            anchors.centerIn: parent
                            radius: 18
                            color: theme.alpha(theme.ink950, 0.82)
                            border.width: 1
                            border.color: theme.alpha(theme.lineStrong, 0.55)
                            implicitWidth: emptyState.implicitWidth + 36
                            implicitHeight: emptyState.implicitHeight + 32

                            ColumnLayout {
                                id: emptyState
                                anchors.centerIn: parent
                                spacing: 10

                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: "Program Monitor"
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: 14
                                    font.weight: Font.Medium
                                }

                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: "Select a video in Library and add it to the timeline."
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                }
                            }
                        }

                        Rectangle {
                            visible: !root.hasPlayableClip()
                                     && timelineDocument.previewSource.length === 0
                                     && root.hasLibraryVideoSelection()
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 12
                            radius: 14
                            color: theme.alpha(theme.ink950, 0.84)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.42)
                            implicitHeight: 56

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 10

                                Label {
                                    text: catalogAssets.selectedCameraLabel.length > 0
                                          ? catalogAssets.selectedCameraLabel
                                          : "Selected video"
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Label {
                                    text: catalogAssets.selectedDurationText.length > 0
                                          ? catalogAssets.selectedDurationText
                                          : "Ready"
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: 10
                                }

                                ActionButton {
                                    text: "Add To Timeline"
                                    tone: "primary"
                                    onClicked: timelineDocument.addClipFromGallery(
                                                   catalogAssets.selectedAssetId,
                                                   catalogAssets.selectedFilePath,
                                                   catalogAssets.selectedCameraLabel,
                                                   0,
                                                   catalogAssets.selectedThumbnailSource)
                                }
                            }
                        }

                        Rectangle {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 12
                            radius: 12
                            color: theme.alpha(theme.ink950, 0.82)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.45)
                            implicitWidth: overlayReadout.implicitWidth + 20
                            implicitHeight: 28

                            Label {
                                id: overlayReadout
                                anchors.centerIn: parent
                                text: timecodeText(sequencePlayheadSeconds)
                                color: theme.textStrong
                                font.family: theme.fontMono
                                font.pixelSize: 11
                            }
                        }

                        Rectangle {
                            visible: timelinePlayback.errorString.length > 0
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 12
                            radius: 14
                            color: theme.alpha("#4f1713", 0.94)
                            border.width: 1
                            border.color: theme.alpha("#e58767", 0.58)
                            implicitHeight: playbackErrorLabel.implicitHeight + 24

                            Label {
                                id: playbackErrorLabel
                                anchors.fill: parent
                                anchors.margins: 12
                                text: "Playback failed: " + timelinePlayback.errorString
                                color: "#ffd7ca"
                                font.family: theme.fontUi
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 16
                        color: theme.alpha(theme.ink950, 0.78)
                        border.width: 1
                        border.color: theme.alpha(theme.lineStrong, 0.46)
                        implicitHeight: 78

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                ActionButton {
                                    text: sequencePlaying ? "Pause" : "Play"
                                    enabled: root.hasPlayableClip()
                                    onClicked: {
                                        if (sequencePlaying) {
                                            timelinePlayback.pause()
                                        } else {
                                            timelinePlayback.play()
                                        }
                                    }
                                }

                                ActionButton {
                                    text: "To Start"
                                    enabled: timelineDocument.clipCount > 0
                                    onClicked: timelinePlayback.seekSequenceSeconds(0)
                                }

                                ActionButton {
                                    text: "-1f"
                                    enabled: root.hasPlayableClip()
                                    onClicked: timelinePlayback.stepFrames(-1)
                                }

                                ActionButton {
                                    text: "+1f"
                                    enabled: root.hasPlayableClip()
                                    onClicked: timelinePlayback.stepFrames(1)
                                }

                                ActionButton {
                                    text: "Split"
                                    tone: "primary"
                                    enabled: root.hasPlayableClip()
                                    onClicked: splitAtPlayhead()
                                }

                                ActionButton {
                                    text: snappingEnabled ? "Snap On" : "Snap Off"
                                    onClicked: snappingEnabled = !snappingEnabled
                                }

                                ActionButton {
                                    text: rippleModeEnabled ? "Ripple On" : "Ripple Off"
                                    enabled: timelineDocument.currentIndex >= 0
                                    onClicked: rippleModeEnabled = !rippleModeEnabled
                                }

                                ActionButton {
                                    text: "Back"
                                    enabled: timelineDocument.currentIndex > 0
                                    onClicked: timelineDocument.moveSelectedBackward()
                                }

                                ActionButton {
                                    text: "Forward"
                                    enabled: timelineDocument.currentIndex >= 0
                                             && timelineDocument.currentIndex < timelineDocument.clipCount - 1
                                    onClicked: timelineDocument.moveSelectedForward()
                                }

                                ActionButton {
                                    text: "Remove"
                                    enabled: timelineDocument.currentIndex >= 0
                                    onClicked: timelineDocument.removeSelectedClip()
                                }

                                Item { Layout.fillWidth: true }

                                InfoPill {
                                    label: "Playhead"
                                    value: formatSeconds(sequencePlayheadSeconds)
                                }
                            }

                            Slider {
                                id: transportSlider
                                Layout.fillWidth: true
                                from: 0
                                to: Math.max(0, sequenceDurationSeconds())
                                value: sequencePlayheadSeconds
                                enabled: timelineDocument.clipCount > 0
                                onPressedChanged: {
                                    if (pressed)
                                        timelinePlayback.pause()
                                }
                                onMoved: timelinePlayback.seekSequenceSeconds(value)
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 286
                Layout.fillHeight: true
                radius: 18
                color: theme.alpha(theme.ink900, 0.82)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.55)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Label {
                        text: "Inspector"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }

                    Label {
                        Layout.fillWidth: true
                        text: currentClip.displayLabel && currentClip.displayLabel.length > 0
                              ? currentClip.displayLabel
                              : "No clip selected"
                        color: theme.textMain
                        font.family: theme.fontUi
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: currentClip.trackLabel
                              ? currentClip.trackLabel + "  •  " + (currentClip.hasAudio ? "Linked audio" : "Silent source")
                              : "Select a clip on the timeline to inspect it."
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 14
                        color: theme.alpha(theme.ink950, 0.72)
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.42)
                        implicitHeight: 176

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8

                            Label {
                                text: "Trim"
                                color: theme.textStrong
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Label {
                                text: "In  •  " + Number(currentClip.trimStartSeconds || 0).toFixed(2) + "s"
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Slider {
                                Layout.fillWidth: true
                                from: 0
                                to: currentClip.durationSeconds ? currentClip.durationSeconds : 1
                                value: currentClip.trimStartSeconds ? currentClip.trimStartSeconds : 0
                                enabled: timelineDocument.currentIndex >= 0
                                onMoved: timelineDocument.setSelectedTrimStartSeconds(value)
                            }

                            Label {
                                text: "Out  •  " + Number(currentClip.trimEndSeconds || 0).toFixed(2) + "s"
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Slider {
                                Layout.fillWidth: true
                                from: 0
                                to: currentClip.durationSeconds ? currentClip.durationSeconds : 1
                                value: currentClip.trimEndSeconds ? currentClip.trimEndSeconds : 0
                                enabled: timelineDocument.currentIndex >= 0
                                onMoved: timelineDocument.setSelectedTrimEndSeconds(value)
                            }

                            Label {
                                text: currentClip.trimmedDurationText
                                      ? "Duration  •  " + currentClip.trimmedDurationText
                                      : "Duration  •  0:00"
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 14
                        color: theme.alpha(theme.ink950, 0.72)
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.42)
                        implicitHeight: 206

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8

                            Label {
                                text: "Frame"
                                color: theme.textStrong
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            ToneComboBox {
                                Layout.fillWidth: true
                                model: ["Fit", "Fill", "Stretch"]
                                currentIndex: currentClip.frameMode === "fill"
                                              ? 1
                                              : currentClip.frameMode === "stretch"
                                                ? 2
                                                : 0
                                enabled: timelineDocument.currentIndex >= 0
                                onActivated: timelineDocument.setSelectedFrameMode(currentText.toLowerCase())
                            }

                            Label {
                                text: "Scale  •  " + Number(currentClip.frameScale || 1).toFixed(2) + "x"
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Slider {
                                Layout.fillWidth: true
                                from: 1
                                to: 2.5
                                value: currentClip.frameScale ? currentClip.frameScale : 1
                                enabled: timelineDocument.currentIndex >= 0
                                onMoved: timelineDocument.setSelectedFrameScale(value)
                            }

                            Label {
                                text: "Horizontal  •  " + Number(currentClip.frameOffsetX || 0).toFixed(2)
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Slider {
                                Layout.fillWidth: true
                                from: -1
                                to: 1
                                value: currentClip.frameOffsetX ? currentClip.frameOffsetX : 0
                                enabled: timelineDocument.currentIndex >= 0
                                onMoved: timelineDocument.setSelectedFrameOffsetX(value)
                            }

                            Label {
                                text: "Vertical  •  " + Number(currentClip.frameOffsetY || 0).toFixed(2)
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Slider {
                                Layout.fillWidth: true
                                from: -1
                                to: 1
                                value: currentClip.frameOffsetY ? currentClip.frameOffsetY : 0
                                enabled: timelineDocument.currentIndex >= 0
                                onMoved: timelineDocument.setSelectedFrameOffsetY(value)
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 14
                        color: theme.alpha(theme.ink950, 0.72)
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.42)
                        implicitHeight: 132

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8

                            Label {
                                text: "Timeline View"
                                color: theme.textStrong
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Label {
                                text: "Zoom  •  " + Math.round(timelineZoom) + " px/s"
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Slider {
                                Layout.fillWidth: true
                                from: 36
                                to: 180
                                value: timelineZoom
                                onMoved: timelineZoom = value
                            }

                            Label {
                                text: "Frames  •  " + Number(timelineDocument.framesPerSecond || 30).toFixed(0) + " fps"
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Label {
                                text: "Drag a video clip horizontally to retime it or vertically to another lane."
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 308
            radius: 18
            color: theme.alpha(theme.ink900, 0.84)
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.55)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        text: "Timeline Canvas"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }

                    Label {
                        text: timelineDocument.trackCount + " tracks"
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Label {
                        text: "Drag video clips between lanes"
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        text: timecodeText(sequencePlayheadSeconds)
                        color: theme.accentWarm300
                        font.family: theme.fontMono
                        font.pixelSize: 11
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    Column {
                        id: trackHeaders
                        width: laneHeaderWidth
                        spacing: trackSpacing

                        Rectangle {
                            width: laneHeaderWidth
                            height: rulerHeight
                            color: "transparent"
                        }

                        Repeater {
                            model: sequenceTrackModel

                            delegate: Rectangle {
                                required property var modelData

                                width: laneHeaderWidth
                                height: laneHeight
                                radius: 10
                                color: theme.alpha(theme.ink950, 0.78)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.42)

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 3

                                    Label {
                                        text: modelData.displayLabel
                                        color: theme.textStrong
                                        font.family: theme.fontMono
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        text: modelData.trackKind === "audio" ? "Linked lane" : "Picture lane"
                                        color: theme.textMuted
                                        font.family: theme.fontMono
                                        font.pixelSize: 9
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "transparent"
                        clip: true

                        Flickable {
                            id: timelineViewport
                            anchors.fill: parent
                            contentWidth: timelineContentWidth
                            contentHeight: timelineContentHeight
                            boundsBehavior: Flickable.StopAtBounds
                            clip: true

                            ScrollBar.horizontal: ScrollBar {
                                policy: ScrollBar.AsNeeded
                            }

                            Rectangle {
                                width: timelineContentWidth
                                height: timelineContentHeight
                                color: "transparent"

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    hoverEnabled: true
                                    onPressed: function(mouse) {
                                        root.seekTimelineFromX(mouse.x)
                                    }
                                    onPositionChanged: function(mouse) {
                                        if (pressed)
                                            root.seekTimelineFromX(mouse.x)
                                    }
                                }

                                Repeater {
                                    model: Math.max(2, Math.ceil(sequenceDurationSeconds()) + 1)

                                    delegate: Item {
                                        required property int index

                                        x: index * timelineZoom
                                        y: 0
                                        width: 1
                                        height: timelineContentHeight

                                        Rectangle {
                                            anchors.top: parent.top
                                            width: 1
                                            height: timelineContentHeight
                                            color: index % 5 === 0 ? theme.alpha(theme.lineStrong, 0.34) : theme.alpha(theme.lineSoft, 0.18)
                                        }

                                        Label {
                                            anchors.top: parent.top
                                            anchors.topMargin: 4
                                            anchors.left: parent.left
                                            anchors.leftMargin: 4
                                            text: root.formatSeconds(index)
                                            color: theme.textMuted
                                            font.family: theme.fontMono
                                            font.pixelSize: 9
                                        }
                                    }
                                }

                                Repeater {
                                    model: sequenceTrackModel

                                    delegate: Item {
                                        id: trackLane
                                        required property var modelData
                                        required property int index

                                        property int trackRowIndex: index

                                        x: 0
                                        y: rulerHeight + index * (laneHeight + trackSpacing)
                                        width: timelineContentWidth
                                        height: laneHeight

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 10
                                            color: theme.alpha(theme.ink950, 0.62)
                                            border.width: 1
                                            border.color: theme.alpha(theme.lineSoft, 0.32)
                                        }

                                        Repeater {
                                            model: modelData.clips

                                            delegate: Rectangle {
                                                id: clipBlock
                                                required property var modelData

                                                property real committedX: Number(modelData.startSeconds || 0) * timelineZoom + 3
                                                property real dragOffsetX: 0
                                                property real dragOffsetY: 0
                                                property real pressX: 0
                                                property real pressY: 0
                                                property real trimHandleWidth: modelData.trackKind === "video" ? 12 : 0

                                                x: committedX + dragOffsetX
                                                y: 6 + dragOffsetY
                                                width: root.clipWidth(modelData.trimmedDurationSeconds) - 6
                                                height: parent.height - 12
                                                radius: 8
                                                color: modelData.trackKind === "audio"
                                                       ? (modelData.silentPlaceholder
                                                          ? theme.alpha(theme.ink700, 0.78)
                                                          : theme.alpha(theme.accentCool400, 0.26))
                                                       : (modelData.selected
                                                          ? theme.alpha(theme.accentWarm500, 0.82)
                                                          : theme.alpha(theme.accentWarm400, 0.34))
                                                border.width: modelData.selected ? 2 : 1
                                                border.color: modelData.selected
                                                              ? theme.textStrong
                                                              : theme.alpha(theme.lineStrong, 0.4)

                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: modelData.trackKind === "video" ? Qt.OpenHandCursor : Qt.PointingHandCursor
                                                    onPressed: function(mouse) {
                                                        clipBlock.pressX = mouse.x
                                                        clipBlock.pressY = mouse.y
                                                        timelineDocument.selectClip(modelData.index)
                                                    }
                                                    onPositionChanged: function(mouse) {
                                                        if (!pressed || modelData.trackKind !== "video")
                                                            return
                                                        clipBlock.dragOffsetX = mouse.x - clipBlock.pressX
                                                        clipBlock.dragOffsetY = mouse.y - clipBlock.pressY
                                                    }
                                                    onReleased: {
                                                        if (modelData.trackKind === "video"
                                                                && (Math.abs(clipBlock.dragOffsetX) > 4 || Math.abs(clipBlock.dragOffsetY) > 4)) {
                                                            const targetRow = Math.max(
                                                                        0,
                                                                        Math.min(
                                                                            sequenceTrackModel.length - 1,
                                                                            trackLane.trackRowIndex + Math.round(clipBlock.dragOffsetY / (laneHeight + trackSpacing))))
                                                            const proposedStart = root.snapSequenceSeconds(
                                                                        Math.max(0, (clipBlock.committedX + clipBlock.dragOffsetX - 3) / timelineZoom),
                                                                        modelData.clipId)
                                                            timelineDocument.moveSelectedClipToTrackRow(targetRow, proposedStart)
                                                        }
                                                        clipBlock.dragOffsetX = 0
                                                        clipBlock.dragOffsetY = 0
                                                    }
                                                }

                                                Image {
                                                    anchors.fill: parent
                                                    anchors.margins: 4
                                                    source: modelData.trackKind === "audio" ? modelData.waveformSource : ""
                                                    fillMode: Image.Stretch
                                                    opacity: 0.42
                                                    visible: modelData.trackKind === "audio"
                                                             && !modelData.silentPlaceholder
                                                             && source.length > 0
                                                    cache: false
                                                    asynchronous: true
                                                }

                                                Rectangle {
                                                    visible: modelData.trackKind === "video"
                                                    anchors.left: parent.left
                                                    anchors.top: parent.top
                                                    anchors.bottom: parent.bottom
                                                    width: clipBlock.trimHandleWidth
                                                    radius: 8
                                                    color: theme.alpha(theme.textStrong, 0.16)

                                                    MouseArea {
                                                        id: leftTrimHandle
                                                        anchors.fill: parent
                                                        cursorShape: Qt.SizeHorCursor
                                                        preventStealing: true
                                                        onPressed: function(mouse) {
                                                            timelinePlayback.pause()
                                                            timelineDocument.selectClip(modelData.index)
                                                            const point = parent.mapToItem(trackLane, mouse.x, mouse.y)
                                                            const proposedStart = root.snapSequenceSeconds(point.x / timelineZoom, modelData.clipId)
                                                            timelineDocument.trimSelectedClipStartToSequenceSeconds(proposedStart, rippleModeEnabled)
                                                        }
                                                        onPositionChanged: function(mouse) {
                                                            if (!pressed)
                                                                return
                                                            const point = parent.mapToItem(trackLane, mouse.x, mouse.y)
                                                            const proposedStart = root.snapSequenceSeconds(point.x / timelineZoom, modelData.clipId)
                                                            timelineDocument.trimSelectedClipStartToSequenceSeconds(proposedStart, rippleModeEnabled)
                                                        }
                                                    }
                                                }

                                                Rectangle {
                                                    visible: modelData.trackKind === "video"
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.bottom: parent.bottom
                                                    width: clipBlock.trimHandleWidth
                                                    radius: 8
                                                    color: theme.alpha(theme.textStrong, 0.16)

                                                    MouseArea {
                                                        id: rightTrimHandle
                                                        anchors.fill: parent
                                                        cursorShape: Qt.SizeHorCursor
                                                        preventStealing: true
                                                        onPressed: function(mouse) {
                                                            timelinePlayback.pause()
                                                            timelineDocument.selectClip(modelData.index)
                                                            const point = parent.mapToItem(trackLane, mouse.x, mouse.y)
                                                            const proposedEnd = root.snapSequenceSeconds(point.x / timelineZoom, modelData.clipId)
                                                            timelineDocument.trimSelectedClipEndToSequenceSeconds(proposedEnd, rippleModeEnabled)
                                                        }
                                                        onPositionChanged: function(mouse) {
                                                            if (!pressed)
                                                                return
                                                            const point = parent.mapToItem(trackLane, mouse.x, mouse.y)
                                                            const proposedEnd = root.snapSequenceSeconds(point.x / timelineZoom, modelData.clipId)
                                                            timelineDocument.trimSelectedClipEndToSequenceSeconds(proposedEnd, rippleModeEnabled)
                                                        }
                                                    }
                                                }

                                                Rectangle {
                                                    visible: modelData.trackKind === "video"
                                                             && (leftTrimHandle.pressed || rightTrimHandle.pressed)
                                                    anchors.horizontalCenter: parent.horizontalCenter
                                                    anchors.bottom: parent.top
                                                    anchors.bottomMargin: 6
                                                    radius: 10
                                                    color: theme.alpha(theme.ink950, 0.9)
                                                    border.width: 1
                                                    border.color: theme.alpha(theme.lineStrong, 0.46)
                                                    implicitWidth: trimOverlayLabel.implicitWidth + 16
                                                    implicitHeight: trimOverlayLabel.implicitHeight + 10

                                                    Label {
                                                        id: trimOverlayLabel
                                                        anchors.centerIn: parent
                                                        text: "In " + Number(modelData.trimStartSeconds || 0).toFixed(2)
                                                              + "  •  Out " + Number(modelData.trimEndSeconds || 0).toFixed(2)
                                                        color: theme.textStrong
                                                        font.family: theme.fontMono
                                                        font.pixelSize: 9
                                                    }
                                                }

                                                ColumnLayout {
                                                    anchors.fill: parent
                                                    anchors.leftMargin: 8 + clipBlock.trimHandleWidth
                                                    anchors.rightMargin: 8 + clipBlock.trimHandleWidth
                                                    anchors.topMargin: 6
                                                    anchors.bottomMargin: 6
                                                    spacing: 2

                                                    Label {
                                                        Layout.fillWidth: true
                                                        text: modelData.displayLabel
                                                        color: theme.textStrong
                                                        font.family: theme.fontUi
                                                        font.pixelSize: 10
                                                        elide: Text.ElideRight
                                                    }

                                                    Label {
                                                        Layout.fillWidth: true
                                                        text: modelData.trackKind === "audio"
                                                              ? (modelData.silentPlaceholder ? "Silent bed" : "Audio lane")
                                                              : modelData.trimmedDurationText
                                                        color: theme.textMuted
                                                        font.family: theme.fontMono
                                                        font.pixelSize: 9
                                                        elide: Text.ElideRight
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    x: sequencePlayheadSeconds * timelineZoom
                                    y: 0
                                    width: 2
                                    height: timelineContentHeight
                                    color: theme.accentWarm500
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: timelineDocument

        function onTimelineNameChanged() {
            timelineNameField.text = timelineDocument.timelineName
        }
    }
}
