pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "."

ColumnLayout {
    id: root
    required property string effectType
    required property var parameters
    required property var controller
    property bool locked: false
    signal parametersEdited(var values)
    readonly property var fields: effectType === "motion_blur" ? [
        {key: "distance", label: qsTr("Distance (px)"), minimum: 0, maximum: 4096, step: 0.25, defaultValue: 20},
        {key: "angle", label: qsTr("Angle (°)"), minimum: -180, maximum: 180, step: 1, defaultValue: 0}
    ] : [
        {key: "sharpenRadius", label: qsTr("Radius (σ px)"), minimum: 0, maximum: 4096, step: 0.25, defaultValue: 2},
        {key: "strength", label: qsTr("Strength (%)"), minimum: 0, maximum: 500, step: 0.25, scale: 100, defaultValue: 100},
        {key: "threshold", label: qsTr("Threshold (%)"), minimum: 0, maximum: 100, step: 0.25, scale: 100, defaultValue: 0}
    ]
    Repeater {
        model: root.fields
        delegate: InspectorSlider {
            id: row
            required property var modelData
            readonly property real scale: Number(modelData.scale || 1)
            objectName: "photoEffectParameter_" + modelData.key
            Layout.fillWidth: true
            label: modelData.label
            from: Number(modelData.minimum)
            to: Number(modelData.maximum)
            stepSize: Number(modelData.step || 0.25)
            value: Number(root.parameters[modelData.key] || 0) * scale
            valueText: Number(value).toFixed(scale > 1 ? 2 : (stepSize % 1 === 0 ? 0 : 2))
            defaultValue: Number(modelData.defaultValue || 0) * scale
            enabled: !root.locked
            showResetButton: false
            onInteractionStarted: root.controller.beginCoalescedEdit()
            onInteractionFinished: root.controller.endCoalescedEdit()
            onValueDragged: function(nextValue) {
                const values = Object.assign({}, root.parameters)
                values[row.modelData.key] = nextValue / row.scale
                root.parametersEdited(values)
            }
        }
    }
}
