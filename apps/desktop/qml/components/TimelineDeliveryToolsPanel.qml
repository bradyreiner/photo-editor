pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    required property var timelineDocument
    readonly property bool packageBusy: timelineDocument.projectPackageBusy === true
    readonly property string packageStatus: String(timelineDocument.projectPackageStatus || "")
    readonly property var packageResult: timelineDocument.projectPackageResult || ({})
    readonly property bool qcBusy: timelineDocument.outputQcBusy === true
    readonly property var qcResult: timelineDocument.outputQcResult || ({})
    readonly property string deliveryToolsContract: "timeline-delivery-tools:v1|package:relative-path-zip-media-luts-compounds-font-disclosure-sha256|write:atomic-no-overwrite-cancel-cleanup|qc:ffprobe-streams-duration-full-decode-black-silence-advisory|ui:project-menu-modular-popup"

    signal closeRequested()

    Theme { id: theme }

    function byteText(bytes) {
        const value = Math.max(0, Number(bytes || 0))
        if (value < 1024)
            return Math.round(value) + " B"
        if (value < 1024 * 1024)
            return (value / 1024).toFixed(value < 10240 ? 1 : 0) + " KB"
        if (value < 1024 * 1024 * 1024)
            return (value / (1024 * 1024)).toFixed(value < 10 * 1024 * 1024 ? 1 : 0) + " MB"
        return (value / (1024 * 1024 * 1024)).toFixed(1) + " GB"
    }

    function streamText(stream) {
        const type = String(stream.type || "stream")
        const codec = String(stream.codec || "unknown codec")
        const ordinal = stream.index !== undefined ? " stream " + String(Number(stream.index) + 1) : ""
        if (type === "video")
            return "Video" + ordinal + " · " + codec + " · " + String(stream.width || 0) + " × " + String(stream.height || 0) +
                   (String(stream.pixelFormat || "").length > 0 ? " · " + String(stream.pixelFormat) : "")
        if (type === "audio")
            return "Audio" + ordinal + " · " + codec + " · " + String(stream.sampleRate || 0) + " Hz · " +
                   String(stream.channels || 0) + " ch"
        return type + " · " + codec
    }

    function timeText(seconds) {
        const totalMilliseconds = Math.max(0, Math.round(Number(seconds || 0) * 1000))
        const hours = Math.floor(totalMilliseconds / 3600000)
        const minutes = Math.floor((totalMilliseconds % 3600000) / 60000)
        const wholeSeconds = Math.floor((totalMilliseconds % 60000) / 1000)
        const milliseconds = totalMilliseconds % 1000
        const pad = function(value, width) {
            return String(value).padStart(width, "0")
        }
        return (hours > 0 ? pad(hours, 2) + ":" : "") +
               pad(minutes, 2) + ":" + pad(wholeSeconds, 2) + "." + pad(milliseconds, 3)
    }

    function spansText(label, spans) {
        const values = spans || []
        const visible = []
        for (let index = 0; index < Math.min(values.length, 6); ++index) {
            const span = values[index] || ({})
            const stream = String(span.streamLabel || "")
            visible.push((stream.length > 0 ? stream + " " : "") +
                         timeText(span.startSeconds) + "–" + timeText(span.endSeconds))
        }
        const remainder = values.length > visible.length
                          ? " · +" + String(values.length - visible.length) + " more" : ""
        return label + ": " + visible.join(" · ") + remainder
    }

    FileDialog {
        id: packageFileDialog
        title: "Collect Portable Timeline Project"
        fileMode: FileDialog.SaveFile
        nameFilters: ["ZIP project archive (*.zip)"]
        defaultSuffix: "zip"
        onAccepted: root.timelineDocument.createPortableProjectPackage(selectedFile.toString())
    }

    FileDialog {
        id: outputFileDialog
        title: "Inspect Rendered Output"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Rendered media (*.mp4 *.mov *.mkv *.mxf *.webm *.wav *.m4a *.aiff *.flac)",
                      "All files (*)"]
        onAccepted: root.timelineDocument.inspectRenderedOutputAsync(selectedFile.toString())
    }

    implicitWidth: 560
    implicitHeight: toolsColumn.implicitHeight + theme.space20 * 2

    ColumnLayout {
        id: toolsColumn
        anchors.fill: parent
        anchors.margins: theme.space20
        spacing: theme.space12

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: theme.space2

                Label {
                    text: "Project Delivery"
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeTitle
                    font.weight: Font.DemiBold
                }

                Label {
                    Layout.fillWidth: true
                    text: "Collect referenced media and check a rendered file before handoff."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    wrapMode: Text.WordWrap
                }
            }

            ActionButton {
                objectName: "timelineDeliveryToolsCloseButton"
                text: "Close"
                iconName: "close"
                compact: true
                tone: "secondary"
                onClicked: root.closeRequested()
            }
        }

        GlassSurface {
            Layout.fillWidth: true
            implicitHeight: packageColumn.implicitHeight + theme.space16 * 2

            ColumnLayout {
                id: packageColumn
                anchors.fill: parent
                anchors.margins: theme.space16
                spacing: theme.space8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.space8

                    Label {
                        Layout.fillWidth: true
                        text: "Portable project"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeSection
                        font.weight: Font.DemiBold
                    }

                    ActionButton {
                        objectName: "timelineCollectProjectPackageButton"
                        text: root.packageBusy ? "Collecting…" : "Collect ZIP…"
                        iconName: "save"
                        tone: "primary"
                        compact: true
                        enabled: !root.packageBusy && root.timelineDocument.hasActiveTimeline
                        disabledReason: root.timelineDocument.hasActiveTimeline
                                        ? "A project package is already being written."
                                        : "Open a timeline before collecting a package."
                        onClicked: packageFileDialog.open()
                    }

                    ActionButton {
                        objectName: "timelineCancelProjectPackageButton"
                        visible: root.packageBusy
                        text: "Cancel"
                        iconName: "close"
                        tone: "secondary"
                        compact: true
                        onClicked: root.timelineDocument.cancelPortableProjectPackage()
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: root.packageBusy ? root.packageStatus
                          : root.packageResult.success === true
                            ? String(root.packageResult.message || root.packageStatus)
                            : root.packageStatus
                    color: root.packageResult.success === false && root.packageResult.status === "failed"
                           ? theme.warning : theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.packageResult.success === true
                    text: String(root.packageResult.fileCount || 0) + " files · " +
                          root.byteText(root.packageResult.size) + " · checksum manifest included"
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.packageResult.success === true &&
                             Number(root.packageResult.fontDependencyCount || 0) > 0
                    text: String(root.packageResult.fontDependencyCount || 0) +
                          " font dependencies are listed in the manifest; font files are not embedded."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WordWrap
                }
            }
        }

        GlassSurface {
            Layout.fillWidth: true
            implicitHeight: qcColumn.implicitHeight + theme.space16 * 2

            ColumnLayout {
                id: qcColumn
                anchors.fill: parent
                anchors.margins: theme.space16
                spacing: theme.space8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.space8

                    Label {
                        Layout.fillWidth: true
                        text: "Rendered output check"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeSection
                        font.weight: Font.DemiBold
                    }

                    ActionButton {
                        objectName: "timelineInspectRenderedOutputButton"
                        text: root.qcBusy ? "Inspecting…" : "Inspect File…"
                        iconName: "info"
                        tone: "secondary"
                        compact: true
                        enabled: !root.qcBusy
                        onClicked: outputFileDialog.open()
                    }

                    ActionButton {
                        objectName: "timelineCancelOutputInspectionButton"
                        visible: root.qcBusy
                        text: "Cancel"
                        iconName: "close"
                        tone: "secondary"
                        compact: true
                        onClicked: root.timelineDocument.cancelRenderedOutputInspection()
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.qcBusy
                    text: "Reading streams, decoding media, and scanning video and audio streams."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: !root.qcBusy && Object.keys(root.qcResult).length > 0
                    text: String(root.qcResult.message || "Output inspection finished.")
                    color: root.qcResult.decodeOk === false ? theme.error : theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: !root.qcBusy && root.qcResult.success === true
                    text: String(root.qcResult.format || "Media") + " · " +
                          Number(root.qcResult.durationSeconds || 0).toFixed(2) + " s · " +
                          root.byteText(root.qcResult.fileSize)
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WordWrap
                }

                Label {
                    objectName: "timelineQcSignalScanCoverage"
                    Layout.fillWidth: true
                    visible: !root.qcBusy && root.qcResult.videoStreamsScanned !== undefined
                    text: "Signal scan: " + String(root.qcResult.videoStreamsScanned || 0) +
                          " video · " + String(root.qcResult.audioStreamsScanned || 0) + " audio streams" +
                          (root.qcResult.signalScanComplete === false ? " · incomplete" : "")
                    color: root.qcResult.signalScanComplete === false ? theme.warning : theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: !root.qcBusy && String(root.qcResult.signalScanWarning || "").length > 0
                    text: String(root.qcResult.signalScanWarning || "")
                    color: theme.warning
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: !root.qcBusy && root.qcResult.streams ? root.qcResult.streams : []

                    delegate: Label {
                        required property var modelData
                        Layout.fillWidth: true
                        text: root.streamText(modelData)
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WordWrap
                    }
                }

                Repeater {
                    model: !root.qcBusy && root.qcResult.findings ? root.qcResult.findings : []

                    delegate: Label {
                        required property var modelData
                        Layout.fillWidth: true
                        text: String(modelData.label || "Review") + "  ·  " +
                              String(modelData.message || "")
                        color: modelData.severity === "error" ? theme.error : theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WordWrap
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: !root.qcBusy && root.qcResult.blackSegments !== undefined &&
                             root.qcResult.blackSegments.length > 0
                    text: root.spansText("Black video", root.qcResult.blackSegments)
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: !root.qcBusy && root.qcResult.silenceSegments !== undefined &&
                             root.qcResult.silenceSegments.length > 0
                    text: root.spansText("Audio silence", root.qcResult.silenceSegments)
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.qcResult.signalsAreAdvisory === true
                    text: "Black and quiet spans are editorial review signals, not automatic failures."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
