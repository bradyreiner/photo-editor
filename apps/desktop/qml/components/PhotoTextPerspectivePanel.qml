pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

ColumnLayout {
    id: root

    required property var controller
    required property int layerIndex
    required property var layerData
    property bool locked: false

    Theme { id: theme }
    spacing: theme.space4

    Label {
        objectName: "photoTextPerspectiveHeading"
        Layout.fillWidth: true
        text: qsTr("Plane perspective")
        font.weight: theme.weightControl
        font.family: theme.fontUi
        font.pixelSize: theme.typeLabel
        color: theme.textMuted
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("Shape the text box along either axis.")
        wrapMode: Text.WordWrap
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeMicro
    }

    GridLayout {
        Layout.fillWidth: true
        columns: root.width >= theme.scaled(600) ? 2 : 1
        columnSpacing: theme.space8
        rowSpacing: theme.space4
        Repeater {
            model: [
                { key: "textPerspectiveHorizontal", label: qsTr("Horizontal") },
                { key: "textPerspectiveVertical", label: qsTr("Vertical") }
            ]

            InspectorSlider {
                required property var modelData
                Layout.fillWidth: true
                objectName: "photoTextPerspective_" + modelData.key
                label: modelData.label
                from: -100
                to: 100
                stepSize: 0.25
                defaultValue: 0
                value: Number(root.layerData[modelData.key] || 0)
                valueText: Number(root.layerData[modelData.key] || 0).toFixed(2).replace(/\.?0+$/, "") + "%"
                enabled: !root.locked
                disabledReason: qsTr("Unlock this text layer to edit its perspective.")
                onInteractionStarted: root.controller.beginCoalescedEdit()
                onInteractionFinished: root.controller.endCoalescedEdit()
                onValueDragged: function(value) {
                    const horizontal = modelData.key === "textPerspectiveHorizontal"
                        ? value : Number(root.layerData.textPerspectiveHorizontal || 0)
                    const vertical = modelData.key === "textPerspectiveVertical"
                        ? value : Number(root.layerData.textPerspectiveVertical || 0)
                    root.controller.setPhotoLayerPerspective(root.layerIndex, horizontal, vertical)
                }
            }
        }
    }
}
