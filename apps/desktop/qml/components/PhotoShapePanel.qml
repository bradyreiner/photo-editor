pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: root
    required property var controller
    required property int layerIndex
    required property var layerData
    property bool locked: false
    readonly property var definition: root.layerData && root.layerData.shapeDefinition
                                      ? root.layerData.shapeDefinition : ({})

    Theme { id: theme }

    function shapeTypeLabel() {
        const type = String(root.definition.type || "shape")
        if (type === "rounded-rectangle") return qsTr("Rounded rectangle")
        return type.charAt(0).toUpperCase() + type.slice(1)
    }
    function applyStyle(fillColor, strokeColor, strokeWidth) {
        const nextFill = fillColor === undefined ? String(root.definition.fillColor || "") : String(fillColor)
        const nextStroke = strokeColor === undefined ? String(root.definition.strokeColor || "") : String(strokeColor)
        const nextWidth = strokeWidth === undefined ? Number(root.definition.strokeWidth || 0) : Number(strokeWidth)
        return root.controller.setPhotoLayerShapeStyle(root.layerIndex, nextFill, nextStroke, nextWidth)
    }
    spacing: theme.space4

    RowLayout {
        Layout.fillWidth: true
        spacing: theme.space4
        AppIcon {
            name: "shape-tool"
            color: theme.textMuted
            Layout.preferredWidth: theme.scaled(16)
            Layout.preferredHeight: theme.scaled(16)
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Edit shape")
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeControl
            font.weight: theme.weightControl
        }
        Label {
            objectName: "photoShapeLayerTypeLabel"
            text: root.shapeTypeLabel()
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
        }
    }
    ShapeFillControls {
        Layout.fillWidth: true
        controller: root.controller
        layerIndex: root.layerIndex
        layerId: String(root.layerData.id || "")
        definition: root.definition
        locked: root.locked
        visible: root.definition.type !== "line" && root.definition.type !== "arrow"
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: theme.space4
        Label { Layout.fillWidth: true; text: qsTr("Stroke"); color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeCaption }
        ColorSwatchButton {
            objectName: "photoShapeLayerStrokeSwatch"
            swatchColor: String(root.definition.strokeColor || "").length > 0 ? String(root.definition.strokeColor) : theme.ink750
            selected: String(root.definition.strokeColor || "").length > 0
            accent: theme.interaction300
            label: String(root.definition.strokeColor || "").length > 0 ? qsTr("Stroke color") : qsTr("Stroke is off")
            enabled: !root.locked
            onPicked: strokeDialog.open()
        }
        MiniButton {
            objectName: "photoShapeLayerNoStrokeButton"
            text: qsTr("No stroke")
            iconName: "close"
            role: "secondary"
            active: String(root.definition.strokeColor || "").length === 0
            tooltip: qsTr("Remove the shape stroke.")
            enabled: !root.locked
            onClicked: root.applyStyle(undefined, "", undefined)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: theme.space4
        Label { text: qsTr("Width"); color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeCaption }
        ToolValueSlider {
            objectName: "photoShapeLayerStrokeWidthSlider"
            Layout.fillWidth: true
            Layout.minimumWidth: theme.scaled(72)
            minimumReadableWidth: theme.scaled(72)
            from: 0
            to: 1
            stepSize: 0.01
            value: Number(root.definition.strokeWidth || 0) <= 0 ? 0
                   : Math.log(Math.max(1, Number(root.definition.strokeWidth))) / Math.log(512)
            label: qsTr("Shape stroke width in image pixels")
            valueText: Number(root.definition.strokeWidth || 0).toFixed(1) + " px"
            tooltipText: qsTr("Shape stroke width: %1 px").arg(Number(root.definition.strokeWidth || 0).toFixed(1))
            accent: theme.interaction300
            enabled: !root.locked && String(root.definition.strokeColor || "").length > 0
            onPressedChanged: {
                if (pressed) root.controller.beginCoalescedEdit()
                else root.controller.endCoalescedEdit()
            }
            onMoved: root.applyStyle(undefined, undefined, value <= 0.01 ? 0 : Math.max(1, Math.round(Math.pow(512, value) * 10) / 10))
        }
        ToneTextField {
            id: widthField
            objectName: "photoShapeLayerStrokeWidthField"
            Layout.preferredWidth: theme.scaled(72)
            Layout.preferredHeight: theme.controlHeightDense
            text: Number(root.definition.strokeWidth || 0).toString()
            font.pixelSize: theme.typeControl
            enabled: !root.locked
            Accessible.name: qsTr("Shape stroke width in image pixels")
            validator: DoubleValidator { bottom: 0; top: 512; decimals: 2; notation: DoubleValidator.StandardNotation; locale: "C" }
            onEditingFinished: {
                if (acceptableInput)
                    root.applyStyle(undefined, undefined, Number(text))
                text = Qt.binding(function() { return Number(root.definition.strokeWidth || 0).toString() })
            }
            Keys.onEscapePressed: function(event) {
                text = Qt.binding(function() { return Number(root.definition.strokeWidth || 0).toString() })
                focus = false
                event.accepted = true
            }
        }
    }

    ColorDialog {
        id: strokeDialog
        title: qsTr("Shape stroke")
        selectedColor: String(root.definition.strokeColor || "").length > 0 ? String(root.definition.strokeColor) : "#ffffff"
        onAccepted: root.applyStyle(undefined, selectedColor.toString(), undefined)
    }
}
