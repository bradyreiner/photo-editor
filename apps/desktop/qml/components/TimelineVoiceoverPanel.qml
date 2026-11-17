pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AppPopup {
    id: root
    objectName: "timelineVoiceoverPanelPopup"

    required property var recorder
    required property var timelineDocument
    property real preRollSeconds: 3
    property bool capturePlaybackPending: false
    property bool capturePlaybackStarted: false

    signal playbackStartRequested()
    signal playbackStopRequested()

    function selectedDeviceIndex() {
        const devices = recorder.inputDevices || []
        const selectedId = String(recorder.selectedInputDeviceId || "")
        for (let index = 0; index < devices.length; ++index) {
            if (String(devices[index].id || "") === selectedId)
                return index
        }
        return devices.length > 0 ? 0 : -1
    }

    function durationText(milliseconds) {
        const totalSeconds = Math.max(0, Math.floor(Number(milliseconds || 0) / 1000))
        const minutes = Math.floor(totalSeconds / 60)
        const seconds = totalSeconds % 60
        return String(minutes).padStart(2, "0") + ":" + String(seconds).padStart(2, "0")
    }

    function fileNameForPath(value) {
        const normalized = String(value || "").replace(/\\/g, "/")
        const fileName = normalized.split("/").pop()
        return fileName.length > 0 ? fileName : normalized
    }

    function captureMatchesCurrentTimeline() {
        const capturedIdentity = String(recorder.captureTimelineIdentity || "")
        const capturedPath = String(recorder.captureDocumentPath || "")
        if (capturedIdentity.length > 0
                && capturedIdentity !== String(timelineDocument.timelineAssetId || ""))
            return false
        if (capturedPath.length > 0
                && capturedPath !== String(timelineDocument.savedDocumentPath || ""))
            return false
        return capturedIdentity.length > 0 || capturedPath.length === 0
    }

    function beginRecording() {
        capturePlaybackPending = true
        capturePlaybackStarted = false
        if (!recorder.startRecording(
                    String(timelineDocument.savedDocumentPath || ""),
                    Math.round(preRollSeconds * 1000),
                    String(timelineDocument.timelineAssetId || ""))) {
            capturePlaybackPending = false
            return
        }
        startPlaybackForCapture()
    }

    function startPlaybackForCapture() {
        if (!capturePlaybackPending ||
                (recorder.state !== "preroll" && recorder.state !== "recording"))
            return
        capturePlaybackPending = false
        capturePlaybackStarted = true
        playbackStartRequested()
    }

    function stopPlaybackForCapture() {
        capturePlaybackPending = false
        if (!capturePlaybackStarted)
            return
        capturePlaybackStarted = false
        playbackStopRequested()
    }

    function stopRecording() {
        recorder.stopRecording()
    }

    function cancelRecording() {
        recorder.cancelRecording()
        stopPlaybackForCapture()
    }

    parent: Overlay.overlay
    modal: true
    focus: true
    width: Math.min(popupAvailableWidth,
                    Math.max(1, Math.min(theme.scaled(460), parent ? parent.width - theme.space24 * 2 : theme.scaled(460))))
    height: Math.min(popupAvailableHeight,
                     Math.max(1, Math.min(implicitHeight, parent ? parent.height - theme.space24 * 2 : implicitHeight)))
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? Math.max(theme.space24, (parent.height - height) / 2) : 0
    padding: theme.popupPadding
    closePolicy: recorder.state === "permissionPending" || recorder.state === "preroll"
                 || recorder.state === "recording" || recorder.state === "finalizing"
                 ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: PopupSurface {
        radius: theme.popupRadius
        shadowColor: "#48000000"
    }

    contentItem: ColumnLayout {
        spacing: theme.space12

        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.space4

            Label {
                text: "Record Voiceover"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeDialogTitle
                font.weight: Font.DemiBold
            }

            Label {
                Layout.fillWidth: true
                text: "Record from the selected input while the timeline plays. Stop adds a WAV take to a dedicated Voiceover lane as one undoable edit."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeLabel
                wrapMode: Text.Wrap
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.space6

            Label {
                text: "INPUT DEVICE"
                color: theme.textFaint
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                font.weight: Font.DemiBold
            }

            ToneComboBox {
                objectName: "timelineVoiceoverInputDevice"
                Layout.fillWidth: true
                accessibleLabel: qsTr("Voiceover input device")
                model: root.recorder.inputDevices
                textRole: "description"
                currentIndex: root.selectedDeviceIndex()
                enabled: root.recorder.state === "idle" || root.recorder.state === "error"
                onActivated: function(index) {
                    const device = root.recorder.inputDevices[index]
                    if (device)
                        root.recorder.selectedInputDeviceId = String(device.id || "")
                }
            }

            ActionButton {
                Layout.alignment: Qt.AlignRight
                text: "Refresh inputs"
                iconName: "refresh"
                compact: true
                enabled: root.recorder.state === "idle" || root.recorder.state === "error"
                onClicked: root.recorder.refreshInputDevices()
            }

            Label {
                Layout.fillWidth: true
                visible: (root.recorder.inputDevices || []).length === 0
                text: "No microphone input is available. Connect an input device and refresh the list."
                color: theme.warning
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.Wrap
            }
        }

        InspectorSlider {
            Layout.fillWidth: true
            label: "Pre-roll"
            valueText: root.preRollSeconds.toFixed(1) + " s"
            from: 0
            to: 10
            stepSize: 0.5
            value: root.preRollSeconds
            defaultValue: 3
            valueInputMode: "raw"
            enabled: root.recorder.state === "idle" || root.recorder.state === "error"
            onValueDragged: function(nextValue) { root.preRollSeconds = nextValue }
            onResetRequested: root.preRollSeconds = 3
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: stateColumn.implicitHeight + theme.space16
            radius: theme.radiusSurface
            color: theme.alpha(theme.ink800, 0.74)
            border.width: 0

            ColumnLayout {
                id: stateColumn
                anchors.fill: parent
                anchors.margins: theme.space8
                spacing: theme.space4

                RowLayout {
                    Layout.fillWidth: true
                    StatusChip {
                        text: root.recorder.state === "permissionPending" ? "MIC ACCESS"
                              : root.recorder.state === "recording" ? "RECORDING"
                              : root.recorder.state === "preroll" ? "PRE-ROLL"
                              : root.recorder.state === "error" ? "INPUT ERROR" : "READY"
                        tone: root.recorder.state === "recording" ? "error"
                              : root.recorder.state === "error" ? "warning" : "neutral"
                        compact: true
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: root.recorder.state === "preroll"
                              ? (Number(root.recorder.preRollRemainingMs || 0) / 1000).toFixed(1) + " s"
                              : root.durationText(root.recorder.recordedDurationMs)
                        color: theme.textStrong
                        font.family: theme.fontNumeric
                        font.features: theme.numericFontFeatures
                        font.pixelSize: theme.typeControl
                        font.weight: Font.DemiBold
                    }
                }

                Label {
                    objectName: "timelineVoiceoverStateMessage"
                    Layout.fillWidth: true
                    text: root.recorder.state === "idle"
                          && String(root.recorder.recoverablePath || "").length > 0
                          ? "Canceled take kept as a recoverable partial WAV. Use Add Audio to place it when ready."
                          : root.recorder.state === "error"
                          ? String(root.recorder.errorString || "Recording failed.")
                          : root.recorder.state === "permissionPending"
                            ? "Waiting for macOS microphone access. Choose Allow in the system prompt, or cancel to return to the timeline."
                            : root.recorder.state === "preroll"
                              ? "Timeline playback is rolling. Recording starts when pre-roll reaches zero."
                              : root.recorder.state === "recording"
                                ? "Recording live input. Stop finalizes the WAV and inserts the take."
                                : "A completed WAV is kept with the timeline's media."
                    color: root.recorder.state === "error" ? theme.warning : theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.Wrap
                }

                Label {
                    id: recoverablePathLabel
                    objectName: "timelineVoiceoverRecoverablePath"
                    Layout.fillWidth: true
                    visible: String(root.recorder.recoverablePath || "").length > 0
                    text: "Saved or recoverable WAV: " + root.fileNameForPath(root.recorder.recoverablePath)
                    color: theme.textFaint
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    Accessible.description: String(root.recorder.recoverablePath || "")
                    HoverHandler { id: recoverablePathHover }
                    AppToolTip {
                        targetItem: recoverablePathLabel
                        visible: recoverablePathLabel.visible && recoverablePathHover.hovered
                        delay: 350
                        text: String(root.recorder.recoverablePath || "")
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space8

            ActionButton {
                objectName: "timelineVoiceoverRecordButton"
                Layout.fillWidth: true
                text: root.recorder.state === "permissionPending" ? "Requesting access…"
                      : root.recorder.state === "preroll" ? "Pre-rolling…" : "Start Recording"
                iconName: "audio"
                enabled: (root.recorder.state === "idle" || root.recorder.state === "error")
                         && (root.recorder.inputDevices || []).length > 0
                onClicked: root.beginRecording()
            }

            ActionButton {
                objectName: "timelineVoiceoverStopButton"
                Layout.fillWidth: true
                text: "Stop · Insert"
                iconName: "check"
                enabled: root.recorder.state === "recording"
                onClicked: root.stopRecording()
            }

            ActionButton {
                objectName: "timelineVoiceoverCancelButton"
                Layout.fillWidth: true
                text: "Cancel"
                iconName: "close"
                enabled: root.recorder.state === "permissionPending"
                         || root.recorder.state === "recording" || root.recorder.state === "preroll"
                onClicked: root.cancelRecording()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "Close"
                compact: true
                enabled: root.recorder.state !== "permissionPending"
                         && root.recorder.state !== "recording" && root.recorder.state !== "preroll"
                onClicked: root.close()
            }
        }
    }

    Connections {
        target: root.recorder
        function onStateChanged() { root.startPlaybackForCapture() }
        function onRecordingFinished(filePath, durationMs) {
            root.stopPlaybackForCapture()
            if (root.recorder.state !== "error" && root.captureMatchesCurrentTimeline())
                root.close()
        }
        function onRecordingCancelled(recoverablePath) {
            root.stopPlaybackForCapture()
            if (String(recoverablePath || "").length === 0)
                root.close()
        }
        function onRecordingFailed(detail, recoverablePath) {
            root.stopPlaybackForCapture()
        }
    }

    Theme { id: theme }
}
