import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    color: "transparent"

    RowLayout {
        anchors.fill: parent
        spacing: 14

        BrandPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "Mask Preview"
            subtitle: "Live image with local adjustment overlay."

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 12
                color: "#14171a"
                border.width: 1
                border.color: "#2d3339"
                clip: true

                Image {
                    id: maskPreview
                    anchors.fill: parent
                    anchors.margins: 18
                    source: editorPreview.previewSource
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    visible: editorPreview.previewSource.length > 0
                }

                Item {
                    visible: editorPreview.linearMaskEnabled && maskPreview.visible
                    anchors.centerIn: maskPreview
                    width: maskPreview.paintedWidth * 1.3
                    height: maskPreview.paintedHeight * 1.3
                    rotation: editorPreview.linearMaskRotation
                    opacity: 0.88

                    Rectangle {
                        anchors.fill: parent
                        radius: 10
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#55c99a52" }
                            GradientStop { position: Math.max(0.0, editorPreview.linearMaskPosition - editorPreview.linearMaskFeather * 0.5); color: "#28c99a52" }
                            GradientStop { position: editorPreview.linearMaskPosition; color: "#208ab8d8" }
                            GradientStop { position: Math.min(1.0, editorPreview.linearMaskPosition + editorPreview.linearMaskFeather * 0.5); color: "#068ab8d8" }
                            GradientStop { position: 1.0; color: "#008ab8d8" }
                        }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 18
                    radius: 10
                    visible: editorPreview.previewSource.length === 0
                    color: "#181c20"
                    border.width: 1
                    border.color: "#2d3339"

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 8

                        Label {
                            text: catalogAssets.selectedAssetId.length > 0 ? "Preview unavailable for this asset" : "Select an image in Library"
                            color: "#f2f4f6"
                            font.family: "Avenir Next"
                            font.pixelSize: 18
                            font.weight: Font.Medium
                        }

                        Label {
                            text: "Masking stays attached to the current selected asset."
                            color: "#8f98a2"
                            font.family: "Avenir Next"
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }

        BrandPanel {
            Layout.preferredWidth: 360
            Layout.fillHeight: true
            title: "Mask Controls"
            subtitle: "Current local adjustment stack."

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: parent.width
                    spacing: 12

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Mask"

                        ActionButton {
                            Layout.fillWidth: true
                            text: editorPreview.linearMaskEnabled ? "Disable Linear Gradient" : "Enable Linear Gradient"
                            active: editorPreview.linearMaskEnabled
                            onClicked: editorPreview.linearMaskEnabled = !editorPreview.linearMaskEnabled
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Adjustment"

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Exposure"
                            valueText: editorPreview.linearMaskExposure.toFixed(2) + " EV"
                            from: -2
                            to: 2
                            value: editorPreview.linearMaskExposure
                            onValueDragged: editorPreview.linearMaskExposure = value
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Warmth"
                            valueText: Math.round(editorPreview.linearMaskTemperature).toString()
                            from: -100
                            to: 100
                            value: editorPreview.linearMaskTemperature
                            onValueDragged: editorPreview.linearMaskTemperature = value
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Shape"

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Position"
                            valueText: editorPreview.linearMaskPosition.toFixed(2)
                            from: 0.0
                            to: 1.0
                            stepSize: 0.01
                            value: editorPreview.linearMaskPosition
                            onValueDragged: editorPreview.linearMaskPosition = value
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Feather"
                            valueText: editorPreview.linearMaskFeather.toFixed(2)
                            from: 0.05
                            to: 1.0
                            stepSize: 0.01
                            value: editorPreview.linearMaskFeather
                            onValueDragged: editorPreview.linearMaskFeather = value
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Rotation"
                            valueText: Math.round(editorPreview.linearMaskRotation).toString() + "°"
                            from: -180
                            to: 180
                            stepSize: 1
                            value: editorPreview.linearMaskRotation
                            onValueDragged: editorPreview.linearMaskRotation = value
                        }
                    }
                }
            }
        }
    }
}
