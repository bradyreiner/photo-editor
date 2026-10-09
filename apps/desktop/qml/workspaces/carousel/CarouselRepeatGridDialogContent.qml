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
    required property var carouselRepeatGridDialog
    anchors.fill: parent
    anchors.margins: 16
    spacing: 10

    RowLayout {
        Layout.fillWidth: true
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label {
                text: "Repeat grid"
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                text: "Repeat the selected layer or grouped cell as ordinary, independently editable layers."
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
            tooltipText: "Cancel repeat grid"
            onClicked: carouselRepeatGridDialog.close()
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 10
        rowSpacing: 6

        InspectorSlider {
            Layout.fillWidth: true
            label: "Rows"
            valueText: Math.round(value).toString()
            from: 1
            to: 10
            stepSize: 1
            value: root.repeatGridRows
            onValueDragged: function(nextValue) {
                root.repeatGridRows = Math.round(nextValue)
                carouselRepeatGridDialog.refreshPreview()
            }
        }
        InspectorSlider {
            Layout.fillWidth: true
            label: "Columns"
            valueText: Math.round(value).toString()
            from: 1
            to: 10
            stepSize: 1
            value: root.repeatGridColumns
            onValueDragged: function(nextValue) {
                root.repeatGridColumns = Math.round(nextValue)
                carouselRepeatGridDialog.refreshPreview()
            }
        }
        InspectorSlider {
            Layout.fillWidth: true
            label: "Gap X"
            valueText: Math.round(value) + " px"
            from: 0
            to: 320
            stepSize: 1
            value: root.repeatGridGapX
            onValueDragged: function(nextValue) {
                root.repeatGridGapX = nextValue
                carouselRepeatGridDialog.refreshPreview()
            }
        }
        InspectorSlider {
            Layout.fillWidth: true
            label: "Gap Y"
            valueText: Math.round(value) + " px"
            from: 0
            to: 320
            stepSize: 1
            value: root.repeatGridGapY
            onValueDragged: function(nextValue) {
                root.repeatGridGapY = nextValue
                carouselRepeatGridDialog.refreshPreview()
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 5
        Label {
            text: "Ordering"
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 10
        }
        FilterChip {
            Layout.fillWidth: true
            text: "Across rows"
            active: root.repeatGridOrdering === "row-major"
            onClicked: {
                root.repeatGridOrdering = "row-major"
                carouselRepeatGridDialog.refreshPreview()
            }
        }
        FilterChip {
            Layout.fillWidth: true
            text: "Down columns"
            active: root.repeatGridOrdering === "column-major"
            onClicked: {
                root.repeatGridOrdering = "column-major"
                carouselRepeatGridDialog.refreshPreview()
            }
        }
    }

    Rectangle {
        id: repeatGridPreviewSurface
        objectName: "carouselRepeatGridPreviewSurface"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 180
        radius: theme.radius8
        color: theme.mediaCanvas
        border.width: 1
        border.color: theme.alpha(theme.lineSoft, 0.5)
        clip: true

        Item {
            id: repeatGridResultBounds
            anchors.centerIn: parent
            width: {
                const resultWidth = Math.max(1, Number(carouselRepeatGridDialog.preview.resultWidth || 1))
                const resultHeight = Math.max(1, Number(carouselRepeatGridDialog.preview.resultHeight || 1))
                const scale = Math.min((repeatGridPreviewSurface.width - 36) / resultWidth,
                                       (repeatGridPreviewSurface.height - 36) / resultHeight)
                return resultWidth * Math.max(0.01, scale)
            }
            height: {
                const resultWidth = Math.max(1, Number(carouselRepeatGridDialog.preview.resultWidth || 1))
                const resultHeight = Math.max(1, Number(carouselRepeatGridDialog.preview.resultHeight || 1))
                const scale = Math.min((repeatGridPreviewSurface.width - 36) / resultWidth,
                                       (repeatGridPreviewSurface.height - 36) / resultHeight)
                return resultHeight * Math.max(0.01, scale)
            }

            Repeater {
                model: carouselRepeatGridDialog.preview.cells || []

                delegate: Rectangle {
                    required property var modelData
                    readonly property real resultWidth: Math.max(
                                                            1, Number(carouselRepeatGridDialog.preview.resultWidth || 1))
                    readonly property real resultHeight: Math.max(
                                                             1, Number(carouselRepeatGridDialog.preview.resultHeight || 1))
                    x: (Number(modelData.x || 0) -
                        Number(carouselRepeatGridDialog.preview.resultX || 0)) /
                       resultWidth * repeatGridResultBounds.width
                    y: (Number(modelData.y || 0) -
                        Number(carouselRepeatGridDialog.preview.resultY || 0)) /
                       resultHeight * repeatGridResultBounds.height
                    width: repeatGridResultBounds.width /
                           Math.max(1, Number(carouselRepeatGridDialog.preview.columns || 1)) * 0.92
                    height: repeatGridResultBounds.height /
                            Math.max(1, Number(carouselRepeatGridDialog.preview.rows || 1)) * 0.92
                    radius: 4
                    color: Boolean(modelData.sourceCell)
                           ? theme.alpha(theme.accentWarm400, 0.35)
                           : theme.alpha(theme.accentCool400, 0.24)
                    border.width: Boolean(modelData.sourceCell) ? 2 : 1
                    border.color: Boolean(modelData.sourceCell)
                                  ? theme.accentWarm400 : theme.accentCool400

                    Label {
                        anchors.centerIn: parent
                        text: Number(modelData.cellIndex || 0) + 1
                        color: theme.textStrong
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: Number(carouselRepeatGridDialog.preview.cellCount || 0) + " cells · " +
                  Number(carouselRepeatGridDialog.preview.projectedLayerCount || 0) +
                  " editable layers · " +
                  Math.round(Number(carouselRepeatGridDialog.preview.resultWidth || 0)) + " × " +
                  Math.round(Number(carouselRepeatGridDialog.preview.resultHeight || 0)) + " px"
            color: theme.textMuted
            font.family: theme.fontMono
            font.pixelSize: 10
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            elide: Text.ElideNone
        }
        StatusChip {
            compact: true
            tone: "success"
            text: "Detached + editable"
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        ActionButton {
            Layout.fillWidth: true
            text: "Cancel"
            onClicked: carouselRepeatGridDialog.close()
        }
        ActionButton {
            objectName: "carouselApplyRepeatGridButton"
            Layout.fillWidth: true
            text: "Create repeat grid"
            tone: "primary"
            enabled: Boolean(carouselRepeatGridDialog.preview.active)
            onClicked: {
                if (carouselDocument.applyRepeatGrid())
                    carouselRepeatGridDialog.close()
            }
        }
    }
}
