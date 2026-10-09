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
    required property var carouselNeighborComparisonDialog
    objectName: "carouselNeighborComparisonKeyboardTarget"
    anchors.fill: parent
    anchors.margins: 18
    spacing: 12
    focus: true
    activeFocusOnTab: true
    Accessible.name: qsTr("Neighboring board comparison")
    Accessible.role: Accessible.Pane

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Left) {
            event.accepted = carouselDocument.selectAdjacentSlide(-1)
        } else if (event.key === Qt.Key_Right) {
            event.accepted = carouselDocument.selectAdjacentSlide(1)
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            carouselNeighborComparisonDialog.openBoard(
                Number(root.neighborComparison.currentIndex || 0))
            event.accepted = true
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                Layout.fillWidth: true
                text: "Neighbor comparison"
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: 22
                font.weight: Font.Bold
                elide: Text.ElideNone
            }

            Label {
                Layout.fillWidth: true
                text: "Live board previews · temporary comparison · never exports"
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 10
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
            }
        }

        ActionButton {
            objectName: "carouselNeighborComparisonPreviousButton"
            compact: true
            iconName: "chevron-left"
            text: "Previous"
            enabled: Boolean(root.neighborComparison.previousAvailable)
            onClicked: carouselDocument.selectAdjacentSlide(-1)
        }

        ActionButton {
            objectName: "carouselNeighborComparisonNextButton"
            compact: true
            iconName: "chevron-right"
            text: "Next"
            enabled: Boolean(root.neighborComparison.nextAvailable)
            onClicked: carouselDocument.selectAdjacentSlide(1)
        }

        ActionButton {
            compact: true
            text: "Close"
            onClicked: carouselNeighborComparisonDialog.close()
        }
    }

    RowLayout {
        objectName: "carouselNeighborComparisonSurface"
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 12

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 260
            radius: theme.radius10
            color: theme.alpha(theme.ink950, 0.78)
            border.width: root.neighborComparison.focusedSide === "left" ? 2 : 1
            border.color: root.neighborComparison.focusedSide === "left"
                          ? theme.accentWarm500 : theme.alpha(theme.lineSoft, 0.46)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: Number(root.neighborComparison.leftBoardNumber || 0) + " · " +
                              String(root.neighborComparison.leftName || "")
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        elide: Text.ElideNone
                    }
                    StatusChip {
                        text: root.neighborComparison.focusedSide === "left"
                              ? "Current" : String(root.neighborComparison.leftRole || "content")
                        tone: root.neighborComparison.focusedSide === "left" ? "accent" : "neutral"
                        compact: true
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: theme.radius8
                    color: "#fbf7f1"
                    clip: true

                    Image {
                        objectName: "carouselNeighborComparisonLeftPreview"
                        anchors.fill: parent
                        anchors.margins: 6
                        source: String(root.neighborComparison.leftPreviewSource || "")
                        sourceSize.width: Number(root.neighborComparison.boardPixelWidth || 1080)
                        sourceSize.height: Number(root.neighborComparison.boardPixelHeight || 1350)
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                    }
                }

                ActionButton {
                    Layout.alignment: Qt.AlignRight
                    compact: true
                    text: "Open board " + Number(root.neighborComparison.leftBoardNumber || 0)
                    tone: root.neighborComparison.focusedSide === "left" ? "primary" : "neutral"
                    onClicked: carouselNeighborComparisonDialog.openBoard(
                                   Number(root.neighborComparison.leftBoardIndex || 0))
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 260
            radius: theme.radius10
            color: theme.alpha(theme.ink950, 0.78)
            border.width: root.neighborComparison.focusedSide === "right" ? 2 : 1
            border.color: root.neighborComparison.focusedSide === "right"
                          ? theme.accentWarm500 : theme.alpha(theme.lineSoft, 0.46)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: Number(root.neighborComparison.rightBoardNumber || 0) + " · " +
                              String(root.neighborComparison.rightName || "")
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        elide: Text.ElideNone
                    }
                    StatusChip {
                        text: root.neighborComparison.focusedSide === "right"
                              ? "Current" : String(root.neighborComparison.rightRole || "content")
                        tone: root.neighborComparison.focusedSide === "right" ? "accent" : "neutral"
                        compact: true
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: theme.radius8
                    color: "#fbf7f1"
                    clip: true

                    Image {
                        objectName: "carouselNeighborComparisonRightPreview"
                        anchors.fill: parent
                        anchors.margins: 6
                        source: String(root.neighborComparison.rightPreviewSource || "")
                        sourceSize.width: Number(root.neighborComparison.boardPixelWidth || 1080)
                        sourceSize.height: Number(root.neighborComparison.boardPixelHeight || 1350)
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                    }
                }

                ActionButton {
                    Layout.alignment: Qt.AlignRight
                    compact: true
                    text: "Open board " + Number(root.neighborComparison.rightBoardNumber || 0)
                    tone: root.neighborComparison.focusedSide === "right" ? "primary" : "neutral"
                    onClicked: carouselNeighborComparisonDialog.openBoard(
                                   Number(root.neighborComparison.rightBoardIndex || 0))
                }
            }
        }
    }
}
