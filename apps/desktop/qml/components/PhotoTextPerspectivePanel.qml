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

    spacing: 4

    Label {
        Layout.fillWidth: true
        text: qsTr("Text Perspective")
        font.weight: Font.DemiBold
    }

    Repeater {
        model: [
            { key: "textPerspectiveHorizontal", label: qsTr("Horizontal") },
            { key: "textPerspectiveVertical", label: qsTr("Vertical") }
        ]

        InspectorSlider {
            required property var modelData
            objectName: "photoTextPerspective_" + modelData.key
            label: modelData.label
            from: -100
            to: 100
            stepSize: 0.25
            defaultValue: 0
            value: Number(root.layerData[modelData.key] || 0)
            valueText: Number(root.layerData[modelData.key] || 0).toFixed(2).replace(/\.?0+$/, "")
            enabled: !root.locked
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
