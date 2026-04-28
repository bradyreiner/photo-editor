import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

GlassSurface {
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
    readonly property var documentController: timelineDocument !== undefined && timelineDocument !== null ? timelineDocument : null
    readonly property var sourceState: sourceClip !== undefined && sourceClip !== null ? sourceClip : ({})
    readonly property var assetCatalog: catalogAssets !== undefined && catalogAssets !== null ? catalogAssets : null
    readonly property bool hasSourceClipValue: documentController !== null && documentController.hasSourceClip
    readonly property real sourceInSecondsValue: documentController !== null ? Number(documentController.sourceInSeconds || 0) : 0
    readonly property real sourceOutSecondsValue: documentController !== null ? Number(documentController.sourceOutSeconds || 0) : 0
    readonly property real framesPerSecondValue: documentController !== null ? Number(documentController.framesPerSecond || 30) : 30
    readonly property int assetCountValue: assetCatalog !== null ? Number(assetCatalog.assetCount || 0) : 0

    readonly property bool hasSource: hasSourceClipValue
                                      && sourceState.mediaType !== undefined

    function sourceUsesPlayer() {
        return hasSource
                && (sourceState.mediaType === "video" || sourceState.mediaType === "audio")
    }

    function sourcePreviewImage() {
        if (!hasSource)
            return ""
        if (sourceState.mediaType === "photo")
            return sourceState.posterSource || ""
        if (sourceState.mediaType === "audio")
            return (sourceState.waveformSource && sourceState.waveformSource.length > 0) ? sourceState.waveformSource : (sourceState.posterSource || "")
        return ""
    }

    radius: 12
    accent: theme.accentWarm400
    accentRail: false
    elevated: true

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
        const fps = Math.max(1, Math.round(framesPerSecondValue))
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
        if (!hasSourceClipValue)
            return
        const bounded = Math.max(0, Math.min(Number(sourceState.durationSeconds || 0), Number(seconds || 0)))
        previewSeconds = bounded
        sourcePreviewPlayer.position = Math.round(bounded * 1000)
    }

    function loadIntoSource(assetId, filePath, displayLabel, thumbnailSource) {
        if (documentController === null)
            return
        documentController.loadSourceClipFromGallery(assetId, filePath, displayLabel, 0, thumbnailSource)
        tabChangeRequested("source")
    }

    function syncSourceMonitor() {
        if (!hasSourceClipValue) {
            sourcePreviewPlayer.stop()
            sourcePreviewPlayer.source = ""
            previewSeconds = 0
            return
        }

        if (!root.sourceUsesPlayer()) {
            sourcePreviewPlayer.stop()
            sourcePreviewPlayer.source = ""
            previewSeconds = Math.max(0, sourceInSecondsValue)
            return
        }

        if (String(sourcePreviewPlayer.source) !== String(sourceState.sourceUrl || ""))
            sourcePreviewPlayer.source = sourceState.sourceUrl || ""
        root.seekSource(Math.max(0, sourceInSecondsValue))
    }

    MediaPlayer {
        id: sourcePreviewPlayer
        audioOutput: sourcePreviewAudio
        videoOutput: sourceVideoMonitor

        onPositionChanged: root.previewSeconds = position / 1000
        onPlaybackStateChanged: {
            if (playbackState !== MediaPlayer.PlayingState && root.hasSourceClipValue)
                root.previewSeconds = position / 1000
        }
    }

    AudioOutput {
        id: sourcePreviewAudio
        volume: 1.0
    }

    Connections {
        target: root.documentController
        ignoreUnknownSignals: true

        function onSourceMonitorChanged() {
            root.syncSourceMonitor()
        }
    }

    Component.onCompleted: root.syncSourceMonitor()

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
                text: root.hasSourceClipValue ? "READY" : "EMPTY"
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
                            text: root.hasSource && sourceState.displayLabel ? sourceState.displayLabel : "Source Monitor"
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
                            visible: root.sourceUsesPlayer() && root.hasSource && sourceState.mediaType === "video"
                            fillMode: VideoOutput.PreserveAspectFit
                        }

                        Image {
                            anchors.fill: parent
                            anchors.margins: 8
                            source: root.sourcePreviewImage()
                            fillMode: Image.PreserveAspectFit
                            visible: root.hasSource && source.length > 0 && !root.sourceUsesPlayer()
                            cache: false
                            asynchronous: false
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: !root.hasSourceClipValue
                            text: "Load a clip into Source"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 11
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.hasSource
                              ? (sourceState.durationText || "") + "  |  In " + formatSeconds(root.sourceInSecondsValue)
                                + "  |  Out " + formatSeconds(root.sourceOutSecondsValue)
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
                            enabled: root.hasSourceClipValue
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
                            enabled: root.hasSourceClipValue
                            onClicked: root.seekSource(root.previewSeconds - 1 / Math.max(1, root.framesPerSecondValue))
                        }

                        ActionButton {
                            iconName: "step-forward"
                            showText: false
                            compact: true
                            tooltipText: "Step forward one frame"
                            enabled: root.hasSourceClipValue
                            onClicked: root.seekSource(root.previewSeconds + 1 / Math.max(1, root.framesPerSecondValue))
                        }

                        ActionButton {
                            text: "In"
                            compact: true
                            tooltipText: "Set mark in"
                            enabled: root.hasSourceClipValue
                            onClicked: if (root.documentController) root.documentController.setSourceInSeconds(root.previewSeconds)
                        }

                        ActionButton {
                            text: "Out"
                            compact: true
                            tooltipText: "Set mark out"
                            enabled: root.hasSourceClipValue
                            onClicked: if (root.documentController) root.documentController.setSourceOutSeconds(root.previewSeconds)
                        }
                    }

                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: root.hasSource && sourceState.durationSeconds ? sourceState.durationSeconds : 1
                        value: root.previewSeconds
                        enabled: root.hasSource
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
                            text: "In  " + root.sourceInSecondsValue.toFixed(2)
                                  + "s  |  Out  " + root.sourceOutSecondsValue.toFixed(2)
                                  + "s  |  Dur  " + formatSeconds(Math.max(0, root.sourceOutSecondsValue - root.sourceInSecondsValue))
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
                            enabled: root.hasSourceClipValue
                            onClicked: if (root.documentController) root.documentController.clearSourceRange()
                        }

                        ActionButton {
                            text: "Append"
                            compact: true
                            enabled: root.hasSourceClipValue
                            onClicked: if (root.documentController) root.documentController.appendSourceClipToTimeline()
                        }

                        ActionButton {
                            text: "Insert"
                            compact: true
                            enabled: root.hasSourceClipValue
                            onClicked: if (root.documentController) root.documentController.insertSourceClipAtPlayhead()
                        }

                        ActionButton {
                            text: "Ovr"
                            compact: true
                            tooltipText: "Overwrite at playhead"
                            enabled: root.hasSourceClipValue
                            onClicked: if (root.documentController) root.documentController.overwriteSourceClipAtPlayhead()
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
                text: root.assetCountValue + " clips"
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 10
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 8
                model: root.assetCatalog

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
