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
    spacing: theme.gapDense

    Label {
        Layout.fillWidth: true
        text: qsTr("Shape · %1").arg(root.shapeTypeLabel())
        color: theme.textStrong
        font.family: theme.fontUi
        font.pixelSize: theme.typeLabel
        font.weight: Font.DemiBold
    }
    RowLayout {
        Layout.fillWidth: true
        visible: root.definition.type !== "line" && root.definition.type !== "arrow"
        Label { Layout.fillWidth: true; text: qsTr("Fill"); color: theme.textMuted }
        ColorSwatchButton {
            objectName: "photoShapeLayerFillSwatch"
            swatchColor: String(root.definition.fillColor || "").length > 0 ? String(root.definition.fillColor) : theme.ink750
            label: String(root.definition.fillColor || "").length > 0 ? qsTr("Fill color") : qsTr("Fill is off")
            enabled: !root.locked
            onPicked: fillDialog.open()
        }
        MiniButton {
            objectName: "photoShapeLayerNoFillButton"
            text: qsTr("None")
            role: "segmented"
            active: String(root.definition.fillColor || "").length === 0
            enabled: !root.locked
            onClicked: root.applyStyle("", undefined, undefined)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Label { Layout.fillWidth: true; text: qsTr("Stroke"); color: theme.textMuted }
        ColorSwatchButton {
            objectName: "photoShapeLayerStrokeSwatch"
            swatchColor: String(root.definition.strokeColor || "").length > 0 ? String(root.definition.strokeColor) : theme.ink750
            label: String(root.definition.strokeColor || "").length > 0 ? qsTr("Stroke color") : qsTr("Stroke is off")
            enabled: !root.locked
            onPicked: strokeDialog.open()
        }
        MiniButton {
            objectName: "photoShapeLayerNoStrokeButton"
            text: qsTr("None")
            role: "segmented"
            active: String(root.definition.strokeColor || "").length === 0
            enabled: !root.locked
            onClicked: root.applyStyle(undefined, "", undefined)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Label { Layout.fillWidth: true; text: qsTr("Stroke width"); color: theme.textMuted }
        ToneTextField {
            id: widthField
            objectName: "photoShapeLayerStrokeWidthField"
            Layout.preferredWidth: theme.scaled(72)
            text: Number(root.definition.strokeWidth || 0).toString()
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
        Label { text: qsTr("px"); color: theme.textMuted }
    }

    ColorDialog {
        id: fillDialog
        title: qsTr("Shape fill")
        selectedColor: String(root.definition.fillColor || "").length > 0 ? String(root.definition.fillColor) : "#4a78ff"
        onAccepted: root.applyStyle(selectedColor.toString(), undefined, undefined)
    }
    ColorDialog {
        id: strokeDialog
        title: qsTr("Shape stroke")
        selectedColor: String(root.definition.strokeColor || "").length > 0 ? String(root.definition.strokeColor) : "#ffffff"
        onAccepted: root.applyStyle(undefined, selectedColor.toString(), undefined)
    }
}
