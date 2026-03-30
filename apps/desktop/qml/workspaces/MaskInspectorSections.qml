import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Column {
    id: root

    required property var editRoot
    spacing: 10

    InspectorSection {
        width: parent.width
        title: "Masks"
        subtitle: "Local selections now live beside the regular adjust controls."

        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 8
            rowSpacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: "Linear"
                onClicked: editorPreview.addMask("linear")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Radial"
                onClicked: editorPreview.addMask("radial")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Luma Range"
                onClicked: editorPreview.addMask("luminance")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Color Range"
                onClicked: editorPreview.addMask("color")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "AI Subject"
                enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                onClicked: {
                    editRoot.clearTransientTools()
                    editorPreview.addSubjectMask()
                }
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Smart Select"
                active: editRoot.objectPickArmed
                enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                onClicked: editRoot.selectTool(editRoot.objectPickArmed ? "" : "smart")
            }
        }

        Label {
            visible: !editorPreview.subjectMaskAvailable
            width: parent.width
            text: "AI Subject and Smart Select require macOS 14 or later."
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: "Avenir Next"
            font.pixelSize: 11
        }
    }

    InspectorSection {
        width: parent.width
        title: "Mask Stack"

        Column {
            width: parent.width
            spacing: 8

            Repeater {
                model: editorPreview.maskStack

                delegate: Rectangle {
                    required property int index
                    required property var modelData

                    width: parent.width
                    radius: 10
                    color: index === editorPreview.activeMaskIndex ? "#21272d" : "#171b1f"
                    border.width: 1
                    border.color: index === editorPreview.activeMaskIndex ? editRoot.maskAccent(modelData) : "#2d3339"
                    implicitHeight: 56

                    MouseArea {
                        anchors.fill: parent
                        onClicked: editorPreview.activeMaskIndex = index
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        Rectangle {
                            width: 10
                            height: 10
                            radius: 5
                            color: editRoot.maskAccent(modelData)
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Label {
                                text: modelData.name
                                color: "#f2f4f6"
                                font.family: "Avenir Next"
                                font.pixelSize: 13
                                font.weight: Font.Medium
                            }

                            Label {
                                text: modelData.kind + " · " + modelData.operation
                                color: "#8f98a2"
                                font.family: "Avenir Next"
                                font.pixelSize: 11
                            }
                        }

                        ActionButton {
                            text: modelData.enabled ? "On" : "Off"
                            active: modelData.enabled
                            onClicked: {
                                editorPreview.activeMaskIndex = index
                                editorPreview.activeMaskEnabled = !modelData.enabled
                            }
                        }
                    }
                }
            }

            Label {
                visible: editorPreview.maskCount === 0
                width: parent.width
                text: "No masks yet. Add a shape, brush, range selector, or smart selection."
                wrapMode: Text.WordWrap
                color: "#8f98a2"
                font.family: "Avenir Next"
                font.pixelSize: 12
            }
        }

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: "Up"
                onClicked: editorPreview.moveActiveMask(-1)
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Down"
                onClicked: editorPreview.moveActiveMask(1)
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Duplicate"
                onClicked: editorPreview.duplicateActiveMask()
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Delete"
                onClicked: editorPreview.removeActiveMask()
            }
        }
    }

    InspectorSection {
        width: parent.width
        title: "Active Mask"
        visible: editorPreview.hasActiveMask

        ToneTextField {
            width: parent.width
            text: editorPreview.activeMaskName
            placeholderText: "Mask name"
            onEditingFinished: editorPreview.activeMaskName = text
        }

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: "Add"
                active: editorPreview.activeMaskOperation === "add"
                onClicked: editorPreview.activeMaskOperation = "add"
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Subtract"
                active: editorPreview.activeMaskOperation === "subtract"
                onClicked: editorPreview.activeMaskOperation = "subtract"
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Intersect"
                active: editorPreview.activeMaskOperation === "intersect"
                onClicked: editorPreview.activeMaskOperation = "intersect"
            }
        }

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: editorPreview.activeMaskEnabled ? "Enabled" : "Disabled"
                active: editorPreview.activeMaskEnabled
                onClicked: editorPreview.activeMaskEnabled = !editorPreview.activeMaskEnabled
            }

            Label {
                Layout.fillWidth: true
                text: editorPreview.activeMaskKind.length > 0 ? editorPreview.activeMaskKind + " mask" : ""
                horizontalAlignment: Text.AlignRight
                color: "#8f98a2"
                font.family: "Avenir Next"
                font.pixelSize: 11
            }
        }
    }

    InspectorSection {
        width: parent.width
        title: "Selection Refine"
        subtitle: "Preview with Smart Select first, then refine the edge only if needed."
        visible: editRoot.activeMaskSupportsSelectionRefine()
        expandable: true
        expanded: editorPreview.activeMaskKind === "subject" || editorPreview.activeMaskKind === "object"

        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 8
            rowSpacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: "Invert"
                active: editorPreview.activeMaskSelectionInverted
                onClicked: editorPreview.activeMaskSelectionInverted = !editorPreview.activeMaskSelectionInverted
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Reset Edge"
                onClicked: {
                    editorPreview.activeMaskSelectionInverted = false
                    editorPreview.activeMaskSelectionExpand = 0
                    editorPreview.activeMaskFeather = editRoot.activeMaskResetFeather()
                }
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Grow"
                onClicked: editRoot.nudgeActiveSelectionExpand(12)
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Shrink"
                onClicked: editRoot.nudgeActiveSelectionExpand(-12)
            }
        }

        InspectorSlider {
            width: parent.width
            label: "Expand / Contract"
            valueText: Math.round(editorPreview.activeMaskSelectionExpand).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskSelectionExpand
            onValueDragged: editorPreview.activeMaskSelectionExpand = value
            onResetRequested: editorPreview.activeMaskSelectionExpand = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Edge Softness"
            valueText: editorPreview.activeMaskFeather.toFixed(2)
            from: editorPreview.activeMaskKind === "polygon" ? 0.0 : 0.05
            to: editorPreview.activeMaskKind === "polygon" ? 0.2 : 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskFeather
            onValueDragged: editorPreview.activeMaskFeather = value
            onResetRequested: editorPreview.activeMaskFeather = editRoot.activeMaskResetFeather()
        }
    }

    InspectorSection {
        width: parent.width
        title: "Local Adjustment"
        visible: editorPreview.hasActiveMask
        expandable: true
        expanded: editRoot.activeMaskHasAdjustments()

        InspectorSlider {
            width: parent.width
            label: "Exposure"
            valueText: editorPreview.activeMaskExposure.toFixed(2) + " EV"
            from: -2
            to: 2
            value: editorPreview.activeMaskExposure
            onValueDragged: editorPreview.activeMaskExposure = value
            onResetRequested: editorPreview.activeMaskExposure = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Warmth"
            valueText: Math.round(editorPreview.activeMaskTemperature).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskTemperature
            onValueDragged: editorPreview.activeMaskTemperature = value
            onResetRequested: editorPreview.activeMaskTemperature = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Hue Shift"
            valueText: Math.round(editorPreview.activeMaskHueShift * 1.8).toString() + "°"
            from: -100
            to: 100
            value: editorPreview.activeMaskHueShift
            onValueDragged: editorPreview.activeMaskHueShift = value
            onResetRequested: editorPreview.activeMaskHueShift = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Saturation"
            valueText: Math.round(editorPreview.activeMaskSaturationShift).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskSaturationShift
            onValueDragged: editorPreview.activeMaskSaturationShift = value
            onResetRequested: editorPreview.activeMaskSaturationShift = 0
        }
    }

    InspectorSection {
        width: parent.width
        title: "Shape"
        visible: editorPreview.hasActiveMask &&
                 (editorPreview.activeMaskKind === "linear" || editorPreview.activeMaskKind === "radial")

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskKind === "linear"
            label: "Position"
            valueText: editorPreview.activeMaskPosition.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskPosition
            onValueDragged: editorPreview.activeMaskPosition = value
            onResetRequested: editorPreview.activeMaskPosition = 0.5
        }

        InspectorSlider {
            width: parent.width
            label: "Rotation"
            valueText: Math.round(editorPreview.activeMaskRotation).toString() + "°"
            from: -180
            to: 180
            stepSize: 1
            value: editorPreview.activeMaskRotation
            onValueDragged: editorPreview.activeMaskRotation = value
            onResetRequested: editorPreview.activeMaskRotation = 0
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskKind === "radial"
            label: "Center X"
            valueText: editorPreview.activeMaskCenterX.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskCenterX
            onValueDragged: editorPreview.activeMaskCenterX = value
            onResetRequested: editorPreview.activeMaskCenterX = 0.5
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskKind === "radial"
            label: "Center Y"
            valueText: editorPreview.activeMaskCenterY.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskCenterY
            onValueDragged: editorPreview.activeMaskCenterY = value
            onResetRequested: editorPreview.activeMaskCenterY = 0.5
        }
    }

    InspectorSection {
        width: parent.width
        title: "Brush"
        visible: editorPreview.activeMaskKind === "brush"

        InspectorSlider {
            width: parent.width
            label: "Brush Size"
            valueText: Math.round(editorPreview.activeMaskBrushSize * 100).toString()
            from: 0.01
            to: 0.35
            stepSize: 0.01
            value: editorPreview.activeMaskBrushSize
            onValueDragged: editorPreview.activeMaskBrushSize = value
            onResetRequested: editorPreview.activeMaskBrushSize = 0.08
        }

        InspectorSlider {
            width: parent.width
            label: "Brush Flow"
            valueText: Math.round(editorPreview.activeMaskBrushFlow * 100).toString() + "%"
            from: 0.05
            to: 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskBrushFlow
            onValueDragged: editorPreview.activeMaskBrushFlow = value
            onResetRequested: editorPreview.activeMaskBrushFlow = 1.0
        }

        RowLayout {
            width: parent.width
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: editorPreview.activeMaskBrushPointCount.toString() + " brush points"
                color: "#8f98a2"
                font.family: "Avenir Next"
                font.pixelSize: 11
            }

            ActionButton {
                text: "Clear"
                onClicked: editorPreview.clearActiveBrushPoints()
            }
        }
    }

    InspectorSection {
        width: parent.width
        title: "Polygon"
        visible: editorPreview.activeMaskKind === "polygon"

        RowLayout {
            width: parent.width
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: editorPreview.activeMaskPolygonPointCount.toString() + " points"
                color: "#8f98a2"
                font.family: "Avenir Next"
                font.pixelSize: 11
            }

            ActionButton {
                text: "Undo Point"
                onClicked: editorPreview.removeLastActivePolygonPoint()
            }

            ActionButton {
                text: "Clear"
                onClicked: editorPreview.clearActivePolygonPoints()
            }
        }
    }

    InspectorSection {
        width: parent.width
        title: "AI Selection"
        visible: editorPreview.activeMaskKind === "subject" || editorPreview.activeMaskKind === "object"

        InspectorSlider {
            width: parent.width
            label: "Feather"
            valueText: editorPreview.activeMaskFeather.toFixed(2)
            from: 0.0
            to: 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskFeather
            onValueDragged: editorPreview.activeMaskFeather = value
            onResetRequested: editorPreview.activeMaskFeather = 0.5
        }

        Label {
            width: parent.width
            text: editorPreview.activeMaskKind === "object"
                  ? "Hover and click on the canvas to preview and pick foreground objects in realtime."
                  : "Subject masks are generated automatically from the image and remain editable in the shared mask stack."
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: "Avenir Next"
            font.pixelSize: 11
        }
    }

    InspectorSection {
        width: parent.width
        title: "Luminance Range"
        visible: editorPreview.activeMaskKind === "luminance"

        InspectorSlider {
            width: parent.width
            label: "Minimum"
            valueText: editorPreview.activeMaskLuminanceMin.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskLuminanceMin
            onValueDragged: editorPreview.activeMaskLuminanceMin = value
            onResetRequested: editorPreview.activeMaskLuminanceMin = 0.2
        }

        InspectorSlider {
            width: parent.width
            label: "Maximum"
            valueText: editorPreview.activeMaskLuminanceMax.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskLuminanceMax
            onValueDragged: editorPreview.activeMaskLuminanceMax = value
            onResetRequested: editorPreview.activeMaskLuminanceMax = 0.8
        }

        InspectorSlider {
            width: parent.width
            label: "Feather"
            valueText: editorPreview.activeMaskRangeFeather.toFixed(2)
            from: 0
            to: 0.5
            stepSize: 0.01
            value: editorPreview.activeMaskRangeFeather
            onValueDragged: editorPreview.activeMaskRangeFeather = value
            onResetRequested: editorPreview.activeMaskRangeFeather = 0.1
        }
    }

    InspectorSection {
        width: parent.width
        title: "Color Range"
        visible: editorPreview.activeMaskKind === "color"

        InspectorSlider {
            width: parent.width
            label: "Hue Center"
            valueText: Math.round(editorPreview.activeMaskHueCenter * 360).toString() + "°"
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskHueCenter
            onValueDragged: editorPreview.activeMaskHueCenter = value
            onResetRequested: editorPreview.activeMaskHueCenter = 0.08
        }

        InspectorSlider {
            width: parent.width
            label: "Hue Width"
            valueText: Math.round(editorPreview.activeMaskHueRange * 360).toString() + "°"
            from: 0.02
            to: 0.5
            stepSize: 0.01
            value: editorPreview.activeMaskHueRange
            onValueDragged: editorPreview.activeMaskHueRange = value
            onResetRequested: editorPreview.activeMaskHueRange = 0.12
        }

        InspectorSlider {
            width: parent.width
            label: "Sat Min"
            valueText: editorPreview.activeMaskSaturationMin.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskSaturationMin
            onValueDragged: editorPreview.activeMaskSaturationMin = value
            onResetRequested: editorPreview.activeMaskSaturationMin = 0.1
        }

        InspectorSlider {
            width: parent.width
            label: "Sat Max"
            valueText: editorPreview.activeMaskSaturationMax.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskSaturationMax
            onValueDragged: editorPreview.activeMaskSaturationMax = value
            onResetRequested: editorPreview.activeMaskSaturationMax = 1.0
        }
    }
}
