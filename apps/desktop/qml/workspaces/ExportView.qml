import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    id: root
    color: "transparent"

    Theme { id: theme }

    readonly property bool hasSelection: catalogAssets.selectedAssetId.length > 0
                                        && catalogAssets.selectedFilePath.length > 0

    function fileNameFromPath(path) {
        if (path.length === 0)
            return "";
        const parts = path.split("/");
        return parts.length > 0 ? parts[parts.length - 1] : path;
    }

    function baseNameFromPath(path) {
        const fileName = fileNameFromPath(path);
        const dotIndex = fileName.lastIndexOf(".");
        return dotIndex > 0 ? fileName.slice(0, dotIndex) : fileName;
    }

    function currentSelectionLabel() {
        if (!hasSelection)
            return "No current selection";
        const baseName = baseNameFromPath(catalogAssets.selectedFilePath);
        return baseName.length > 0 ? baseName : catalogAssets.selectedAssetId;
    }

    function currentSelectionSummary() {
        if (!hasSelection)
            return "Select an asset in Library to queue an export.";

        if (catalogAssets.selectedMediaType === "photo") {
            return editorPreview.previewSource.length > 0
                    ? "Photo export uses the current rendered preview for preset-based exports."
                    : "Photo export will fall back to the source file until a preview is available.";
        }

        if (catalogAssets.selectedMediaType === "timeline")
            return "Timeline export copies the saved rendered timeline media.";

        if (catalogAssets.selectedMediaType === "video")
            return "Video export copies the selected media file into the destination queue.";

        return "The current selection can be queued for export.";
    }

    function queueCurrentSelection() {
        exportJobs.queueExport(
                    catalogAssets.selectedAssetId,
                    catalogAssets.selectedMediaType,
                    currentSelectionLabel(),
                    catalogAssets.selectedFilePath,
                    editorPreview.previewSource,
                    presetCombo.currentText,
                    destinationField.text)
    }

    Component.onCompleted: {
        if (destinationField.text.length === 0)
            destinationField.text = exportJobs.defaultDestinationDirectory();
    }

    RowLayout {
        anchors.fill: parent
        spacing: 14

        BrandPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "Export Queue"
            subtitle: exportJobs.busy ? "Processing queued exports." : exportJobs.status

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: exportJobs.jobCount + (exportJobs.jobCount === 1 ? " job" : " jobs")
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: 10
                }

                Item { Layout.fillWidth: true }

                ActionButton {
                    text: "Clear Finished"
                    enabled: exportJobs.jobCount > 0
                    onClicked: exportJobs.clearFinishedJobs()
                }
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: parent.width
                    spacing: 10

                    Rectangle {
                        Layout.fillWidth: true
                        visible: exportJobs.jobCount === 0
                        implicitHeight: 132
                        radius: theme.radius12
                        color: theme.ink900
                        border.width: 1
                        border.color: theme.lineSoft

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 8

                            Label {
                                text: "No queued exports"
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: 16
                                font.weight: Font.Medium
                                Layout.alignment: Qt.AlignHCenter
                            }

                            Label {
                                text: "Choose a destination and queue the current Library selection."
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 12
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }
                    }

                    Repeater {
                        model: exportJobs.jobs

                        delegate: Rectangle {
                            required property var modelData

                            Layout.fillWidth: true
                            implicitHeight: detailsColumn.implicitHeight + 22
                            radius: theme.radius12
                            color: theme.ink900
                            border.width: 1
                            border.color: modelData.status === "Completed"
                                          ? theme.alpha(theme.accentCool500, 0.55)
                                          : modelData.status === "Failed"
                                            ? theme.alpha("#d96a5b", 0.68)
                                            : theme.lineSoft

                            ColumnLayout {
                                id: detailsColumn
                                anchors.fill: parent
                                anchors.margins: 11
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        Label {
                                            text: modelData.displayLabel
                                            color: theme.textStrong
                                            font.family: theme.fontUi
                                            font.pixelSize: 14
                                            font.weight: Font.Medium
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            text: modelData.preset + "  •  " + modelData.status
                                            color: theme.textMuted
                                            font.family: theme.fontMono
                                            font.pixelSize: 10
                                            elide: Text.ElideRight
                                        }
                                    }

                                    ActionButton {
                                        text: "Retry"
                                        visible: modelData.status === "Failed"
                                        onClicked: exportJobs.retryJob(modelData.jobId)
                                    }

                                    ActionButton {
                                        text: "Remove"
                                        enabled: modelData.status !== "Running"
                                        onClicked: exportJobs.removeJob(modelData.jobId)
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.outputPath.length > 0 ? modelData.outputPath
                                                                          : modelData.destinationDirectory
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                    wrapMode: Text.WrapAnywhere
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 6
                                    radius: 3
                                    color: theme.alpha(theme.ink750, 0.92)
                                    visible: modelData.status === "Running" || modelData.status === "Completed"

                                    Rectangle {
                                        width: parent.width * Math.max(0.05, Math.min(1.0, modelData.progress / 100.0))
                                        height: parent.height
                                        radius: parent.radius
                                        color: modelData.status === "Completed" ? theme.accentCool500 : theme.accentWarm500
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    visible: modelData.errorMessage.length > 0
                                    text: modelData.errorMessage
                                    color: "#f4b7af"
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }
            }
        }

        BrandPanel {
            Layout.preferredWidth: 380
            Layout.fillHeight: true
            title: "Job Setup"
            subtitle: "Preset, destination, and selection context."

            InspectorSection {
                Layout.fillWidth: true
                title: "Preset"

                ToneComboBox {
                    id: presetCombo
                    Layout.fillWidth: true
                    model: ["Full Resolution", "Web Proof", "Client Preview", "Quick Web", "Original Copy"]
                }
            }

            InspectorSection {
                Layout.fillWidth: true
                title: "Destination"

                ToneTextField {
                    id: destinationField
                    Layout.fillWidth: true
                    placeholderText: exportJobs.defaultDestinationDirectory()
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ActionButton {
                        Layout.fillWidth: true
                        text: "Browse"
                        onClicked: {
                            const selectedFolder = exportJobs.pickDestinationFolder()
                            if (selectedFolder.length > 0)
                                destinationField.text = selectedFolder
                        }
                    }

                    ActionButton {
                        Layout.fillWidth: true
                        text: "Reset"
                        onClicked: destinationField.text = exportJobs.defaultDestinationDirectory()
                    }
                }
            }

            InspectorSection {
                Layout.fillWidth: true
                title: "Selection Context"

                Label {
                    text: currentSelectionLabel()
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Label {
                    text: hasSelection ? catalogAssets.selectedMediaType : "none"
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: 10
                    Layout.fillWidth: true
                }

                Label {
                    text: currentSelectionSummary()
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            Item { Layout.fillHeight: true }

            ActionButton {
                Layout.fillWidth: true
                text: exportJobs.busy ? "Queue Export" : "Queue Export"
                tone: "primary"
                enabled: hasSelection && destinationField.text.trim().length > 0
                onClicked: root.queueCurrentSelection()
            }
        }
    }
}
