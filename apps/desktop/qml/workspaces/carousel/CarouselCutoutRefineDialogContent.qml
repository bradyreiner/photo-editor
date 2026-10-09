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
    required property var carouselCutoutRefineDialog
    anchors.fill: parent
    anchors.margins: 18
    spacing: 10

    Label {
        Layout.fillWidth: true
        text: "Refine cutout edge"
        color: theme.textStrong
        font.family: theme.fontDisplay
        font.pixelSize: 21
        font.weight: Font.DemiBold
        elide: Text.ElideNone
    }

    Label {
        Layout.fillWidth: true
        text: "Changes update the result and remain reversible through Undo. Use Matte or Edges in the inspector to audit the boundary."
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: 10
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }

    InspectorSlider {
        Layout.fillWidth: true
        label: "Feather"
        valueText: Number(value).toFixed(1) + " px"
        from: 0
        to: 16
        stepSize: 0.5
        value: Number(root.selection.cutoutFeather || 0)
        accent: theme.accentCool400
        onValueDragged: function(nextValue) {
            carouselDocument.setSelectedMediaCutoutRefinement(
                        nextValue,
                        Number(root.selection.cutoutExpand || 0),
                        Number(root.selection.cutoutEdgeContrast || 0))
        }
        onResetRequested: carouselDocument.setSelectedMediaCutoutRefinement(
                              0,
                              Number(root.selection.cutoutExpand || 0),
                              Number(root.selection.cutoutEdgeContrast || 0))
    }

    InspectorSlider {
        Layout.fillWidth: true
        label: "Expand"
        valueText: Number(value).toFixed(1) + " px"
        from: -16
        to: 16
        stepSize: 0.5
        value: Number(root.selection.cutoutExpand || 0)
        accent: theme.accentWarm400
        onValueDragged: function(nextValue) {
            carouselDocument.setSelectedMediaCutoutRefinement(
                        Number(root.selection.cutoutFeather || 0),
                        nextValue,
                        Number(root.selection.cutoutEdgeContrast || 0))
        }
        onResetRequested: carouselDocument.setSelectedMediaCutoutRefinement(
                              Number(root.selection.cutoutFeather || 0),
                              0,
                              Number(root.selection.cutoutEdgeContrast || 0))
    }

    InspectorSlider {
        Layout.fillWidth: true
        label: "Edge contrast"
        valueText: Math.round(value) + "%"
        from: -45
        to: 100
        stepSize: 1
        value: Number(root.selection.cutoutEdgeContrast || 0)
        accent: theme.success
        onValueDragged: function(nextValue) {
            carouselDocument.setSelectedMediaCutoutRefinement(
                        Number(root.selection.cutoutFeather || 0),
                        Number(root.selection.cutoutExpand || 0),
                        nextValue)
        }
        onResetRequested: carouselDocument.setSelectedMediaCutoutRefinement(
                              Number(root.selection.cutoutFeather || 0),
                              Number(root.selection.cutoutExpand || 0),
                              0)
    }

    Item { Layout.fillHeight: true }

    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        ActionButton {
            text: "Done"
            tone: "primary"
            compact: true
            onClicked: carouselCutoutRefineDialog.close()
        }
    }
}
