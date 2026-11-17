pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var previewController
    property bool sampling: false
    readonly property bool colorRange: previewController.activeMaskKind === "color"
    readonly property var samples: colorRange ? previewController.activeMaskColorSamples : []
    readonly property bool unionSamples: colorRange && samples.length > 0 &&
        String((previewController.maskStack[previewController.activeMaskIndex] || ({})).colorSampleMode || "") === "union"
    readonly property string disabledReason: String(previewController.activeMaskEditDisabledReason || "")
    signal samplingRequested(bool enabled)
    spacing: 8

    Theme { id: theme }
    MiniButton {
        objectName: "selectionRangeSampleButton"
        Layout.fillWidth: true
        text: root.sampling ? "Cancel sampling" : root.colorRange ? (root.samples.length ? "Add color" : "Sample color") : "Sample brightness"
        iconName: "eyedropper"
        showLeadingIcon: true
        active: root.sampling
        enabled: root.sampling || (root.disabledReason.length === 0 && (!root.colorRange || root.samples.length < 5))
        tooltip: root.disabledReason || (root.colorRange ? "Pick a color on the canvas. Each pick adds another color." : "Drag a sample area on the canvas.")
        onClicked: root.samplingRequested(!root.sampling)
    }
    Flow {
        Layout.fillWidth: true
        spacing: 6
        visible: root.colorRange && root.samples.length > 0
        Repeater {
            model: root.samples.length
            delegate: RowLayout {
                id: sampleChip
                required property int index
                readonly property var sample: root.samples[sampleChip.index] || ({})
                spacing: 2
                Rectangle {
                    Layout.preferredWidth: 22; Layout.preferredHeight: 22; radius: 5
                    color: String(sampleChip.sample.hex || "#888888")
                    border.width: 1; border.color: Qt.alpha(theme.textMain, 0.25)
                }
                MiniButton {
                    objectName: "selectionRangeRemoveSample" + sampleChip.index
                    text: "×"
                    enabled: root.disabledReason.length === 0
                    tooltip: "Remove sampled color " + (sampleChip.index + 1)
                    onClicked: root.previewController.removeActiveColorRangeSample(sampleChip.index)
                }
            }
        }
    }
    InspectorSlider {
        enabled: root.disabledReason.length === 0
        objectName: "selectionRangeHueCenter"
        Layout.fillWidth: true
        visible: root.colorRange && !root.unionSamples
        label: "Hue center"; from: 0; to: 1; stepSize: 0.01
        value: root.previewController.activeMaskHueCenter
        valueText: Math.round(value * 360) + "°"
        onValueDragged: function(value) { root.previewController.activeMaskHueCenter = value }
        onInteractionStarted: root.previewController.beginCoalescedEdit()
        onInteractionFinished: root.previewController.endCoalescedEdit()
    }
    InspectorSlider {
        enabled: root.disabledReason.length === 0
        objectName: "selectionRangeHueWidth"
        Layout.fillWidth: true
        visible: root.colorRange
        label: "Color tolerance"; from: 0.02; to: 0.5; stepSize: 0.01
        value: root.previewController.activeMaskHueRange
        valueText: Math.round(value * 360) + "°"
        onValueDragged: function(value) { root.previewController.activeMaskHueRange = value }
        onInteractionStarted: root.previewController.beginCoalescedEdit()
        onInteractionFinished: root.previewController.endCoalescedEdit()
    }
    InspectorSlider {
        enabled: root.disabledReason.length === 0
        objectName: "selectionRangeMinimum"
        Layout.fillWidth: true
        label: root.colorRange ? "Saturation min" : "Brightness min"
        from: 0; to: 1; stepSize: 0.01
        value: root.colorRange ? root.previewController.activeMaskSaturationMin : root.previewController.activeMaskLuminanceMin
        valueText: Math.round(value * 100) + "%"
        onValueDragged: function(value) {
            if (root.colorRange) root.previewController.activeMaskSaturationMin = value
            else root.previewController.activeMaskLuminanceMin = value
        }
        onInteractionStarted: root.previewController.beginCoalescedEdit()
        onInteractionFinished: root.previewController.endCoalescedEdit()
    }
    InspectorSlider {
        enabled: root.disabledReason.length === 0
        objectName: "selectionRangeMaximum"
        Layout.fillWidth: true
        label: root.colorRange ? "Saturation max" : "Brightness max"
        from: 0; to: 1; stepSize: 0.01
        value: root.colorRange ? root.previewController.activeMaskSaturationMax : root.previewController.activeMaskLuminanceMax
        valueText: Math.round(value * 100) + "%"
        onValueDragged: function(value) {
            if (root.colorRange) root.previewController.activeMaskSaturationMax = value
            else root.previewController.activeMaskLuminanceMax = value
        }
        onInteractionStarted: root.previewController.beginCoalescedEdit()
        onInteractionFinished: root.previewController.endCoalescedEdit()
    }
    InspectorSlider {
        enabled: root.disabledReason.length === 0
        objectName: "selectionRangeSoftness"
        Layout.fillWidth: true
        label: "Softness"; from: 0; to: 0.5; stepSize: 0.01
        value: root.previewController.activeMaskRangeFeather
        valueText: Math.round(value * 100) + "%"
        onValueDragged: function(value) { root.previewController.activeMaskRangeFeather = value }
        onInteractionStarted: root.previewController.beginCoalescedEdit()
        onInteractionFinished: root.previewController.endCoalescedEdit()
    }
}
