pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia
import QtQml
import "../../components"

ColumnLayout {
    required property var root
    required property var theme
    required property var carouselDocument
    required property var carouselSelectionBatchDialog
    anchors.fill: parent
    anchors.margins: 16
    spacing: 10

    RowLayout {
        Layout.fillWidth: true

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label {
                text: String(carouselSelectionBatchDialog.preview.operationLabel || "Batch change") + " preview"
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                text: "Anchor: " + String(carouselSelectionBatchDialog.preview.anchorLabel || "layer") +
                      " · " + Number(carouselSelectionBatchDialog.preview.affectedCount || 0) +
                      " affected" +
                      (Number(carouselSelectionBatchDialog.preview.skippedCount || 0) > 0
                       ? " · " + Number(carouselSelectionBatchDialog.preview.skippedCount) + " incompatible skipped"
                       : "")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 10
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
            }
        }

        ActionButton {
            compact: true
            showText: false
            iconName: "close"
            tooltipText: "Cancel batch change"
            onClicked: carouselSelectionBatchDialog.close()
        }
    }

    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 5
        model: carouselSelectionBatchDialog.preview.entries || []

        delegate: Rectangle {
            id: batchPreviewRow
            required property var modelData
            width: ListView.view.width
            height: batchPreviewContent.implicitHeight + 14
            radius: theme.radius6
            color: theme.alpha(theme.ink850, 0.82)
            border.width: 1
            border.color: Boolean(modelData.compatible)
                          ? theme.alpha(theme.lineSoft, 0.4)
                          : theme.alpha(theme.warning, 0.55)

            ColumnLayout {
                id: batchPreviewContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 7
                spacing: 3

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: "Board " + Number(batchPreviewRow.modelData.boardNumber || 1) +
                              " · " + String(batchPreviewRow.modelData.displayLabel || "Layer")
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        elide: Text.ElideNone
                    }
                    StatusChip {
                        compact: true
                        tone: Boolean(batchPreviewRow.modelData.compatible) ? "success" : "warning"
                        text: Boolean(batchPreviewRow.modelData.compatible) ? "Will change" : "Skipped"
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: "Before · " + String(batchPreviewRow.modelData.before || "")
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: 9
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    elide: Text.ElideNone
                }
                Label {
                    Layout.fillWidth: true
                    text: "Result · " + String(batchPreviewRow.modelData.result || "")
                    color: Boolean(batchPreviewRow.modelData.compatible) ? theme.textMain : theme.warning
                    font.family: theme.fontMono
                    font.pixelSize: 9
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    elide: Text.ElideNone
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        ActionButton {
            Layout.fillWidth: true
            text: "Cancel"
            onClicked: carouselSelectionBatchDialog.close()
        }
        ActionButton {
            objectName: "carouselApplySelectionBatchButton"
            Layout.fillWidth: true
            text: "Apply in one step"
            tone: "primary"
            enabled: Boolean(carouselSelectionBatchDialog.preview.active) &&
                     Number(carouselSelectionBatchDialog.preview.affectedCount || 0) > 0
            onClicked: {
                if (carouselDocument.applySelectionBatchOperation())
                    carouselSelectionBatchDialog.close()
            }
        }
    }
}
