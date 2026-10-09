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
    required property var carouselContinuityPreviewDialog
    anchors.fill: parent
    anchors.margins: 18
    spacing: 12

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                Layout.fillWidth: true
                text: "Continuity preview"
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: 22
                font.weight: Font.Bold
                elide: Text.ElideNone
            }

            Label {
                Layout.fillWidth: true
                text: "Rendered boards " + (Number(root.continuityPreview.leftBoardIndex || 0) + 1) +
                      " and " + (Number(root.continuityPreview.rightBoardIndex || 1) + 1) +
                      " · actual export cut at " + Number(root.continuityPreview.actualCutX || 0) + " px"
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 10
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
            }
        }

        StatusChip {
            text: root.continuityPreview.hasCriticalSeamContent
                  ? root.continuityPreview.criticalSeamCount + " seam warning" +
                    (root.continuityPreview.criticalSeamCount === 1 ? "" : "s")
                  : "Cut clear"
            tone: root.continuityPreview.hasCriticalSeamContent ? "warning" : "success"
            compact: true
            detailText: root.continuityPreview.hasCriticalSeamContent
                        ? "One or more editable layers cross the exact exported cut."
                        : "No editable layer crosses the exact exported cut."
        }

        ActionButton {
            compact: true
            text: "Close"
            onClicked: carouselContinuityPreviewDialog.close()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        Label {
            text: "Simulated platform gutter"
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 10
        }

        Repeater {
            model: [
                { label: "None", pixels: 0 },
                { label: "Tight 8 px", pixels: 8 },
                { label: "Standard 16 px", pixels: 16 },
                { label: "Wide 24 px", pixels: 24 }
            ]

            delegate: FilterChip {
                required property var modelData
                text: modelData.label
                active: root.continuityGutterPixels === modelData.pixels
                onClicked: root.continuityGutterPixels = modelData.pixels
            }
        }

        Item { Layout.fillWidth: true }

        Label {
            text: "Simulation only · never exported"
            color: theme.textFaint
            font.family: theme.fontMono
            font.pixelSize: 9
        }
    }

    Rectangle {
        id: continuityPreviewStage
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 300
        radius: theme.radius8
        color: theme.alpha(theme.ink950, 0.86)
        border.width: 1
        border.color: theme.alpha(theme.lineSoft, 0.4)
        clip: true

        Row {
            id: continuityBoardRow
            anchors.centerIn: parent
            spacing: root.continuityGutterPixels
            readonly property real boardAspect: Number(root.continuityPreview.boardPixelWidth || 1080) /
                                                Math.max(1, Number(root.continuityPreview.boardPixelHeight || 1350))
            height: Math.min(parent.height - 42,
                             (parent.width - spacing - 42) / Math.max(0.1, boardAspect * 2))
            width: height * boardAspect * 2 + spacing

            Repeater {
                model: [
                    {
                        source: String(root.continuityPreview.leftPreviewSource || ""),
                        boardNumber: Number(root.continuityPreview.leftBoardIndex || 0) + 1,
                        side: "left"
                    },
                    {
                        source: String(root.continuityPreview.rightPreviewSource || ""),
                        boardNumber: Number(root.continuityPreview.rightBoardIndex || 1) + 1,
                        side: "right"
                    }
                ]

                delegate: Item {
                    id: continuityBoard
                    required property var modelData
                    width: continuityBoardRow.height * continuityBoardRow.boardAspect
                    height: continuityBoardRow.height

                    Rectangle {
                        anchors.fill: parent
                        color: "#0a0a0a"
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.72)
                    }

                    Image {
                        anchors.fill: parent
                        source: continuityBoard.modelData.source
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        asynchronous: true
                        smooth: true
                        sourceSize.width: Number(root.continuityPreview.boardPixelWidth || 1080)
                        sourceSize.height: Number(root.continuityPreview.boardPixelHeight || 1350)
                    }

                    Rectangle {
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.right: continuityBoard.modelData.side === "left" ? parent.right : undefined
                        anchors.left: continuityBoard.modelData.side === "right" ? parent.left : undefined
                        width: Math.max(3, Number(root.continuityPreview.unsafeBandPixels || 48) /
                                        Math.max(1, Number(root.continuityPreview.boardPixelWidth || 1080)) * parent.width)
                        color: theme.alpha(theme.warning, root.continuityPreview.hasCriticalSeamContent ? 0.22 : 0.10)
                        border.width: 1
                        border.color: theme.alpha(theme.warning, 0.72)
                    }

                    Rectangle {
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.right: continuityBoard.modelData.side === "left" ? parent.right : undefined
                        anchors.left: continuityBoard.modelData.side === "right" ? parent.left : undefined
                        width: 2
                        color: root.continuityPreview.hasCriticalSeamContent ? theme.warning : theme.success
                    }

                    StatusChip {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 8
                        text: "Board " + continuityBoard.modelData.boardNumber
                        compact: true
                        detailText: "Exact rendered board preview"
                    }
                }
            }
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            text: root.continuityGutterPixels === 0
                  ? "Exact exported edges touching"
                  : root.continuityGutterPixels + " px simulated feed gutter between exact exported edges"
            color: theme.textFaint
            font.family: theme.fontMono
            font.pixelSize: 9
            elide: Text.ElideNone
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: continuityWarningContent.implicitHeight + 20
        radius: theme.radius8
        color: theme.alpha(root.continuityPreview.hasCriticalSeamContent ? theme.warning : theme.success, 0.10)
        border.width: 1
        border.color: theme.alpha(root.continuityPreview.hasCriticalSeamContent ? theme.warning : theme.success, 0.42)

        ColumnLayout {
            id: continuityWarningContent
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: 10
            spacing: 5

            Label {
                Layout.fillWidth: true
                text: root.continuityPreview.hasCriticalSeamContent
                      ? "Critical content crosses the unsafe seam"
                      : "No editable content crosses the unsafe seam"
                color: root.continuityPreview.hasCriticalSeamContent ? theme.warning : theme.success
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: Font.DemiBold
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
            }

            Repeater {
                model: root.continuityPreview.seamWarnings || []

                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        text: String(modelData.message || "Layer crosses the exported cut.")
                        color: theme.textMain
                        font.family: theme.fontUi
                        font.pixelSize: 10
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        elide: Text.ElideNone
                    }

                    ActionButton {
                        compact: true
                        text: "Select layer"
                        onClicked: {
                            carouselDocument.focusPreflightIssue(
                                        Number(modelData.boardIndex || 0),
                                        String(modelData.itemId || ""))
                            carouselContinuityPreviewDialog.close()
                        }
                    }
                }
            }
        }
    }
}
