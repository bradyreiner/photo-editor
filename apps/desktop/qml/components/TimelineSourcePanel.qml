import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

Rectangle {
    id: root

    required property var theme
    required property var timelineDocument
    required property var sourceClip
    required property var catalogAssets
    required property bool collapsed
    required property string activeTab
    signal toggleCollapseRequested()
    signal tabChangeRequested(string tab)
    signal mediaPickerRequested()

    property string mediaSearchText: ""
    property real previewSeconds: 0

    function sourceUsesPlayer() {
        return timelineDocument.hasSourceClip
                && (sourceClip.mediaType === "video" || sourceClip.mediaType === "audio")
    }

    function sourcePreviewImage() {
        if (!timelineDocument.hasSourceClip)
            return ""
        if (sourceClip.mediaType === "photo")
            return sourceClip.posterSource
        if (sourceClip.mediaType === "audio")
            return sourceClip.waveformSource.length > 0 ? sourceClip.waveformSource : sourceClip.posterSource
        return ""
    }

    radius: 12
    color: theme.alpha(theme.ink900, 0.82)
    border.width: 1
    border.color: theme.alpha(theme.lineSoft, 0.55)

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

    function seekSource(seconds) {
        if (!timelineDocument.hasSourceClip)
            return
        const bounded = Math.max(0, Math.min(Number(sourceClip.durationSeconds || 0), Number(seconds || 0)))
        previewSeconds = bounded
        sourcePreviewPlayer.position = Math.round(bounded * 1000)
    }

    function loadIntoSource(assetId, filePath, displayLabel, thumbnailSource) {
        timelineDocument.loadSourceClipFromGallery(assetId, filePath, displayLabel, 0, thumbnailSource)
        tabChangeRequested("source")
    }

    MediaPlayer {
        id: sourcePreviewPlayer
        audioOutput: sourcePreviewAudio
        videoOutput: sourceVideoMonitor

        onPositionChanged: root.previewSeconds = position / 1000
        onPlaybackStateChanged: {
            if (playbackState !== MediaPlayer.PlayingState && timelineDocument.hasSourceClip)
                root.previewSeconds = position / 1000
        }
    }

    AudioOutput {
        id: sourcePreviewAudio
        volume: 1.0
    }

    Connections {
        target: timelineDocument

        function onSourceMonitorChanged() {
            if (!timelineDocument.hasSourceClip) {
                sourcePreviewPlayer.stop()
                sourcePreviewPlayer.source = ""
                root.previewSeconds = 0
                return
            }

            if (!root.sourceUsesPlayer()) {
                sourcePreviewPlayer.stop()
                sourcePreviewPlayer.source = ""
                root.previewSeconds = Math.max(0, timelineDocument.sourceInSeconds)
                return
            }

            if (String(sourcePreviewPlayer.source) !== String(sourceClip.sourceUrl))
                sourcePreviewPlayer.source = sourceClip.sourceUrl
            root.seekSource(Math.max(0, timelineDocument.sourceInSeconds))
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                text: "Source"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            Item { Layout.fillWidth: true }

            ActionButton {
                iconName: collapsed ? "chevron-right" : "chevron-left"
                showText: false
                compact: true
                tooltipText: collapsed ? "Expand source panel" : "Collapse source panel"
                onClicked: toggleCollapseRequested()
            }
        }

        ColumnLayout {
            visible: collapsed
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: timelineDocument.hasSourceClip ? "READY" : "EMPTY"
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 10
                horizontalAlignment: Text.AlignHCenter
            }

            ActionButton {
                Layout.fillWidth: true
                iconName: "source"
                showText: false
                compact: true
                tooltipText: "Source monitor"
                active: activeTab === "source"
                onClicked: tabChangeRequested("source")
            }

            ActionButton {
                Layout.fillWidth: true
                iconName: "media"
                showText: false
                compact: true
                tooltipText: "Media browser"
                active: activeTab === "media"
                onClicked: tabChangeRequested("media")
            }

            Item { Layout.fillHeight: true }

            ActionButton {
                Layout.fillWidth: true
                iconName: "plus"
                showText: false
                compact: true
                tooltipText: "Open media picker"
                tone: "primary"
                onClicked: mediaPickerRequested()
            }
        }

        RowLayout {
            visible: !collapsed
            Layout.fillWidth: true
            spacing: 6

            ActionButton {
                text: "Source"
                compact: true
                active: activeTab === "source"
                onClicked: tabChangeRequested("source")
            }

            ActionButton {
                text: "Media"
                compact: true
                active: activeTab === "media"
                onClicked: tabChangeRequested("media")
            }

            Item { Layout.fillWidth: true }

            ActionButton {
                text: "Load"
                iconName: "plus"
                compact: true
                tooltipText: "Open media picker"
                tone: "primary"
                onClicked: mediaPickerRequested()
            }
        }

        ColumnLayout {
            visible: !collapsed && activeTab === "source"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                radius: 10
                color: theme.alpha(theme.ink950, 0.72)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.42)
                implicitHeight: 348

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            text: timelineDocument.hasSourceClip ? sourceClip.displayLabel : "Source Monitor"
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Label {
                            text: timecodeText(previewSeconds)
                            color: theme.accentWarm300
                            font.family: theme.fontMono
                            font.pixelSize: 10
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 166
                        radius: 8
                        color: "#0b1014"
                        clip: true

                        VideoOutput {
                            id: sourceVideoMonitor
                            anchors.fill: parent
                            anchors.margins: 8
                            visible: root.sourceUsesPlayer() && sourceClip.mediaType === "video"
                            fillMode: VideoOutput.PreserveAspectFit
                        }

                        Image {
                            anchors.fill: parent
                            anchors.margins: 8
                            source: root.sourcePreviewImage()
                            fillMode: Image.PreserveAspectFit
                            visible: timelineDocument.hasSourceClip && source.length > 0 && !root.sourceUsesPlayer()
                            cache: false
                            asynchronous: false
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: !timelineDocument.hasSourceClip
                            text: "Load a clip into Source"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 11
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: timelineDocument.hasSourceClip
                              ? sourceClip.durationText + "  |  In " + formatSeconds(timelineDocument.sourceInSeconds)
                                + "  |  Out " + formatSeconds(timelineDocument.sourceOutSeconds)
                              : "Load a clip to set timing and edit points."
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        ActionButton {
                            iconName: sourcePreviewPlayer.playbackState === MediaPlayer.PlayingState ? "pause" : "play"
                            showText: false
                            compact: true
                            tooltipText: sourcePreviewPlayer.playbackState === MediaPlayer.PlayingState ? "Pause source" : "Play source"
                            enabled: timelineDocument.hasSourceClip
                            onClicked: {
                                if (sourcePreviewPlayer.playbackState === MediaPlayer.PlayingState)
                                    sourcePreviewPlayer.pause()
                                else
                                    sourcePreviewPlayer.play()
                            }
                        }

                        ActionButton {
                            iconName: "step-back"
                            showText: false
                            compact: true
                            tooltipText: "Step back one frame"
                            enabled: timelineDocument.hasSourceClip
                            onClicked: root.seekSource(root.previewSeconds - 1 / Math.max(1, timelineDocument.framesPerSecond))
                        }

                        ActionButton {
                            iconName: "step-forward"
                            showText: false
                            compact: true
                            tooltipText: "Step forward one frame"
                            enabled: timelineDocument.hasSourceClip
                            onClicked: root.seekSource(root.previewSeconds + 1 / Math.max(1, timelineDocument.framesPerSecond))
                        }

                        ActionButton {
                            text: "In"
                            compact: true
                            tooltipText: "Set mark in"
                            enabled: timelineDocument.hasSourceClip
                            onClicked: timelineDocument.setSourceInSeconds(root.previewSeconds)
                        }

                        ActionButton {
                            text: "Out"
                            compact: true
                            tooltipText: "Set mark out"
                            enabled: timelineDocument.hasSourceClip
                            onClicked: timelineDocument.setSourceOutSeconds(root.previewSeconds)
                        }
                    }

                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: sourceClip.durationSeconds ? sourceClip.durationSeconds : 1
                        value: root.previewSeconds
                        enabled: timelineDocument.hasSourceClip
                        onMoved: root.seekSource(value)
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 8
                        color: theme.alpha(theme.ink900, 0.82)
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.32)
                        implicitHeight: 34

                        Label {
                            anchors.fill: parent
                            anchors.margins: 8
                            text: "In  " + Number(timelineDocument.sourceInSeconds || 0).toFixed(2)
                                  + "s  |  Out  " + Number(timelineDocument.sourceOutSeconds || 0).toFixed(2)
                                  + "s  |  Dur  " + formatSeconds(Math.max(0, Number(timelineDocument.sourceOutSeconds || 0) - Number(timelineDocument.sourceInSeconds || 0)))
                            color: theme.textMain
                            font.family: theme.fontMono
                            font.pixelSize: 9
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        ActionButton {
                            text: "Clear"
                            compact: true
                            enabled: timelineDocument.hasSourceClip
                            onClicked: timelineDocument.clearSourceRange()
                        }

                        ActionButton {
                            text: "Append"
                            compact: true
                            enabled: timelineDocument.hasSourceClip
                            onClicked: timelineDocument.appendSourceClipToTimeline()
                        }

                        ActionButton {
                            text: "Insert"
                            compact: true
                            enabled: timelineDocument.hasSourceClip
                            onClicked: timelineDocument.insertSourceClipAtPlayhead()
                        }

                        ActionButton {
                            text: "Ovr"
                            compact: true
                            tooltipText: "Overwrite at playhead"
                            enabled: timelineDocument.hasSourceClip
                            onClicked: timelineDocument.overwriteSourceClipAtPlayhead()
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }

        ColumnLayout {
            visible: !collapsed && activeTab === "media"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            TextField {
                Layout.fillWidth: true
                text: root.mediaSearchText
                placeholderText: "Search visible media"
                color: theme.textStrong
                font.family: theme.fontUi
                selectByMouse: true
                onTextChanged: root.mediaSearchText = text

                background: Rectangle {
                    radius: 8
                    color: theme.ink900
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.42)
                }
            }

            Label {
                text: catalogAssets.assetCount + " clips"
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 10
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 8
                model: catalogAssets

                delegate: Rectangle {
                    required property string assetId
                    required property string cameraLabel
                    required property string filePath
                    required property string thumbnailSource
                    required property string mediaType
                    required property string durationText

                    readonly property string titleText: cameraLabel.length > 0 ? cameraLabel : assetId
                    readonly property bool matchesSearch: root.mediaSearchText.length === 0
                                                         || titleText.toLowerCase().indexOf(root.mediaSearchText.toLowerCase()) >= 0
                    visible: mediaType === "video" && matchesSearch
                    width: ListView.view.width
                    height: visible ? 88 : 0
                    radius: 8
                    color: theme.alpha(theme.ink900, 0.9)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.34)

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 84
                            Layout.preferredHeight: 64
                            radius: 6
                            color: "#0b1014"
                            clip: true

                            Image {
                                anchors.fill: parent
                                source: thumbnailSource
                                fillMode: Image.PreserveAspectCrop
                                cache: false
                                asynchronous: true
                                visible: source.toString().length > 0
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Label {
                                Layout.fillWidth: true
                                text: titleText
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: 11
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.fillWidth: true
                                text: "Video  •  " + durationText
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 9
                                elide: Text.ElideRight
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                ActionButton {
                                    text: "Source"
                                    compact: true
                                    onClicked: root.loadIntoSource(assetId, filePath, titleText, thumbnailSource)
                                }

                                ActionButton {
                                    text: "Ins"
                                    compact: true
                                    tooltipText: "Insert at playhead"
                                    onClicked: timelineDocument.insertClipFromGallery(assetId, filePath, titleText, 0, 0, 0, thumbnailSource)
                                }

                                ActionButton {
                                    text: "Ovr"
                                    compact: true
                                    tooltipText: "Overwrite at playhead"
                                    onClicked: timelineDocument.overwriteClipFromGallery(assetId, filePath, titleText, 0, 0, 0, thumbnailSource)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
