pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    required property var timelineDocument
    signal closeRequested()

    Theme { id: theme }

    property string importUrl: ""
    property string exportUrl: ""
    property var importPreview: ({})
    property var exportPreview: ({})
    property string importMessage: ""
    property string exportMessage: ""
    property bool acceptImportLosses: false
    property bool acceptExportLosses: false

    function fileNameForLocation(value) {
        let location = String(value || "").replace(/^file:\/\/(?:localhost)?/, "")
        try {
            location = decodeURIComponent(location)
        } catch (error) {
            // Keep the encoded filename if the URL contains an invalid escape.
        }
        const normalized = location.replace(/\\/g, "/").split(/[?#]/)[0]
        const fileName = normalized.split("/").pop()
        return fileName.length > 0 ? fileName : location
    }

    function previewImport(url) {
        root.importUrl = String(url || "")
        root.importPreview = root.timelineDocument.previewTimelineOtioImport(root.importUrl)
        root.acceptImportLosses = false
        root.importMessage = root.importPreview.valid === true
                            ? String(root.importPreview.trackCount || 0) + " tracks · " +
                              String(root.importPreview.clipCount || 0) + " referenced clips"
                            : (root.importPreview.errors || []).join("\n")
    }

    function previewExport(url) {
        root.exportUrl = String(url || "")
        root.exportPreview = root.timelineDocument.previewTimelineOtioExport(root.exportUrl)
        root.acceptExportLosses = false
        root.exportMessage = root.exportPreview.valid === true
                            ? String(root.exportPreview.trackCount || 0) + " tracks · " +
                              String(root.exportPreview.clipCount || 0) + " referenced clips"
                            : (root.exportPreview.errors || []).join("\n")
    }

    function applyImport() {
        const result = root.timelineDocument.importTimelineOtio(
            root.importUrl,
            String(root.importPreview.sourcePath || ""),
            String(root.importPreview.sourceSha256 || ""),
            String(root.importPreview.timelineId || ""),
            String(root.importPreview.documentHash || ""),
            root.acceptImportLosses)
        root.importMessage = String(result.message || "OTIO import did not complete.")
        if (result.status === "stale-preview") {
            root.importPreview = ({})
        }
    }

    function writeExport() {
        const result = root.timelineDocument.exportTimelineOtio(
            root.exportUrl,
            String(root.exportPreview.documentHash || ""),
            root.acceptExportLosses)
        root.exportMessage = String(result.message || "OTIO export did not complete.")
        if (result.status === "stale-preview") {
            root.exportPreview = ({})
        }
    }

    FileDialog {
        id: importDialog
        title: "Preview OTIO Timeline Import"
        fileMode: FileDialog.OpenFile
        nameFilters: ["OpenTimelineIO JSON (*.otio *.json)", "All files (*)"]
        onAccepted: root.previewImport(selectedFile.toString())
    }

    FileDialog {
        id: exportDialog
        title: "Preview OTIO Timeline Export"
        fileMode: FileDialog.SaveFile
        nameFilters: ["OpenTimelineIO JSON (*.otio)", "All files (*)"]
        defaultSuffix: "otio"
        onAccepted: root.previewExport(selectedFile.toString())
    }

    implicitWidth: 560
    implicitHeight: interchangeColumn.implicitHeight + theme.space20 * 2

    ColumnLayout {
        id: interchangeColumn
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
                    text: "OTIO Interchange"
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeTitle
                    font.weight: Font.DemiBold
                }

                Label {
                    Layout.fillWidth: true
                    text: "Supported subset: media references, tracks, cuts, gaps, and source trims. Effects, grades, transitions, retimes, nested edits, captions, and output settings are reported as losses."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    wrapMode: Text.WordWrap
                }
            }

            ActionButton {
                objectName: "timelineInterchangeCloseButton"
                text: "Close"
                iconName: "close"
                compact: true
                tone: "secondary"
                onClicked: root.closeRequested()
            }
        }

        GlassSurface {
            Layout.fillWidth: true
            implicitHeight: importColumn.implicitHeight + theme.space16 * 2

            ColumnLayout {
                id: importColumn
                anchors.fill: parent
                anchors.margins: theme.space16
                spacing: theme.space8

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: "Import timeline"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeSection
                        font.weight: Font.DemiBold
                    }
                    ActionButton {
                        objectName: "timelinePreviewOtioImportButton"
                        text: "Choose OTIO…"
                        iconName: "media"
                        tone: "secondary"
                        compact: true
                        enabled: root.timelineDocument.hasActiveTimeline
                        onClicked: importDialog.open()
                    }
                }

                Label {
                    id: importLocationLabel
                    Layout.fillWidth: true
                    visible: root.importUrl.length > 0
                    text: root.fileNameForLocation(root.importUrl)
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    Accessible.description: root.importUrl
                    HoverHandler { id: importLocationHover }
                    AppToolTip {
                        targetItem: importLocationLabel
                        visible: importLocationLabel.visible && importLocationHover.hovered
                        delay: 350
                        text: root.importUrl
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.importMessage.length > 0
                    text: root.importMessage
                    color: root.importPreview.valid === true ? theme.textMuted : theme.warning
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: (root.importPreview.lossyFields || []).length > 0
                    text: "Import limitations:\n• " + (root.importPreview.lossyFields || []).join("\n• ")
                    color: theme.warning
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WordWrap
                }

                ToneCheckBox {
                    objectName: "timelineAcceptOtioImportLossesCheckBox"
                    Layout.fillWidth: true
                    visible: (root.importPreview.lossyFields || []).length > 0
                    checked: root.acceptImportLosses
                    text: "I reviewed and accept these import limitations"
                    onToggled: root.acceptImportLosses = checked
                }

                ActionButton {
                    objectName: "timelineApplyOtioImportButton"
                    text: "Apply import"
                    iconName: "check"
                    tone: "primary"
                    compact: true
                    enabled: root.importPreview.valid === true &&
                             ((root.importPreview.lossyFields || []).length === 0 || root.acceptImportLosses)
                    disabledReason: "Preview an OTIO file and accept any listed limitations before applying."
                    onClicked: root.applyImport()
                }
            }
        }

        GlassSurface {
            Layout.fillWidth: true
            implicitHeight: exportColumn.implicitHeight + theme.space16 * 2

            ColumnLayout {
                id: exportColumn
                anchors.fill: parent
                anchors.margins: theme.space16
                spacing: theme.space8

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: "Export timeline"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeSection
                        font.weight: Font.DemiBold
                    }
                    ActionButton {
                        objectName: "timelinePreviewOtioExportButton"
                        text: "Choose destination…"
                        iconName: "save"
                        tone: "secondary"
                        compact: true
                        enabled: root.timelineDocument.hasActiveTimeline
                        onClicked: exportDialog.open()
                    }
                }

                Label {
                    id: exportLocationLabel
                    Layout.fillWidth: true
                    visible: root.exportUrl.length > 0
                    text: root.fileNameForLocation(root.exportUrl)
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    Accessible.description: root.exportUrl
                    HoverHandler { id: exportLocationHover }
                    AppToolTip {
                        targetItem: exportLocationLabel
                        visible: exportLocationLabel.visible && exportLocationHover.hovered
                        delay: 350
                        text: root.exportUrl
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.exportMessage.length > 0
                    text: root.exportMessage
                    color: root.exportPreview.valid === true ? theme.textMuted : theme.warning
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: (root.exportPreview.lossyFields || []).length > 0
                    text: "Export limitations:\n• " + (root.exportPreview.lossyFields || []).join("\n• ")
                    color: theme.warning
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WordWrap
                }

                ToneCheckBox {
                    objectName: "timelineAcceptOtioExportLossesCheckBox"
                    Layout.fillWidth: true
                    visible: (root.exportPreview.lossyFields || []).length > 0
                    checked: root.acceptExportLosses
                    text: "I reviewed and accept these export limitations"
                    onToggled: root.acceptExportLosses = checked
                }

                ActionButton {
                    objectName: "timelineWriteOtioExportButton"
                    text: "Write OTIO file"
                    iconName: "save"
                    tone: "primary"
                    compact: true
                    enabled: root.exportPreview.valid === true &&
                             ((root.exportPreview.lossyFields || []).length === 0 || root.acceptExportLosses)
                    disabledReason: "Preview a destination and accept any listed limitations before writing."
                    onClicked: root.writeExport()
                }
            }
        }
    }
}
