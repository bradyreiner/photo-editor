import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    id: root

    color: "transparent"

    property string activeMaskTool: "linear"

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value));
    }

    function previewImageX() {
        return maskPreview.x + (maskPreview.width - maskPreview.paintedWidth) / 2;
    }

    function previewImageY() {
        return maskPreview.y + (maskPreview.height - maskPreview.paintedHeight) / 2;
    }

    function currentMaskExposure() {
        return activeMaskTool === "radial" ? editorPreview.radialMaskExposure : editorPreview.linearMaskExposure;
    }

    function currentMaskTemperature() {
        return activeMaskTool === "radial" ? editorPreview.radialMaskTemperature : editorPreview.linearMaskTemperature;
    }

    function ensureToolEnabled(tool) {
        activeMaskTool = tool;
        if (tool === "radial")
            editorPreview.radialMaskEnabled = true;
        else
            editorPreview.linearMaskEnabled = true;
    }

    Component.onCompleted: {
        if (editorPreview.radialMaskEnabled && !editorPreview.linearMaskEnabled)
            activeMaskTool = "radial";
    }

    RowLayout {
        anchors.fill: parent
        spacing: 14

        BrandPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "Mask Preview"
            subtitle: "Place and shape local masks directly on the image."

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
                    visible: maskPreview.visible
                    x: previewImageX()
                    y: previewImageY()
                    width: maskPreview.paintedWidth
                    height: maskPreview.paintedHeight

                    Item {
                        id: linearOverlay
                        anchors.centerIn: parent
                        width: parent.width * 1.35
                        height: parent.height * 1.35
                        rotation: editorPreview.linearMaskRotation
                        opacity: editorPreview.linearMaskEnabled ? (activeMaskTool === "linear" ? 0.92 : 0.45) : 0.0
                        visible: editorPreview.linearMaskEnabled

                        Rectangle {
                            anchors.fill: parent
                            radius: 10
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "#55c99a52" }
                                GradientStop {
                                    position: Math.max(0.0, editorPreview.linearMaskPosition - editorPreview.linearMaskFeather * 0.5)
                                    color: "#28c99a52"
                                }
                                GradientStop { position: editorPreview.linearMaskPosition; color: "#208ab8d8" }
                                GradientStop {
                                    position: Math.min(1.0, editorPreview.linearMaskPosition + editorPreview.linearMaskFeather * 0.5)
                                    color: "#068ab8d8"
                                }
                                GradientStop { position: 1.0; color: "#008ab8d8" }
                            }
                        }

                        Rectangle {
                            x: 0
                            y: Math.max(0, Math.min(linearOverlay.height - 1, linearOverlay.height * editorPreview.linearMaskPosition))
                            width: linearOverlay.width
                            height: 1
                            color: "#f4f7fa"
                            opacity: 0.85
                        }

                        Rectangle {
                            x: 0
                            y: Math.max(0, Math.min(linearOverlay.height - 1,
                                                    linearOverlay.height * Math.max(0, editorPreview.linearMaskPosition - editorPreview.linearMaskFeather * 0.5)))
                            width: linearOverlay.width
                            height: 1
                            color: "#d6dde5"
                            opacity: 0.45
                        }

                        Rectangle {
                            x: 0
                            y: Math.max(0, Math.min(linearOverlay.height - 1,
                                                    linearOverlay.height * Math.min(1, editorPreview.linearMaskPosition + editorPreview.linearMaskFeather * 0.5)))
                            width: linearOverlay.width
                            height: 1
                            color: "#d6dde5"
                            opacity: 0.45
                        }

                        Rectangle {
                            id: linearCenterHandle
                            x: linearOverlay.width / 2 - width / 2
                            y: linearOverlay.height * editorPreview.linearMaskPosition - height / 2
                            width: 16
                            height: 16
                            radius: 8
                            color: "#f4f7fa"
                            border.width: 1
                            border.color: "#101419"

                            MouseArea {
                                anchors.fill: parent
                                enabled: activeMaskTool === "linear"
                                cursorShape: Qt.SizeVerCursor
                                onPressed: root.activeMaskTool = "linear"
                                onPositionChanged: function(mouse) {
                                    if (!pressed)
                                        return;
                                    editorPreview.linearMaskPosition = clamp(
                                                (linearCenterHandle.y + mouse.y + linearCenterHandle.height / 2) / linearOverlay.height,
                                                0,
                                                1);
                                }
                            }
                        }

                        Rectangle {
                            id: linearRotationHandle
                            x: linearOverlay.width / 2 - width / 2
                            y: -28
                            width: 14
                            height: 14
                            radius: 7
                            color: "#c99a52"
                            border.width: 1
                            border.color: "#101419"
                            visible: activeMaskTool === "linear"

                            MouseArea {
                                anchors.fill: parent
                                enabled: activeMaskTool === "linear"
                                cursorShape: Qt.CrossCursor
                                onPressed: root.activeMaskTool = "linear"
                                onPositionChanged: function(mouse) {
                                    if (!pressed)
                                        return;
                                    const dx = mouse.x + parent.x - linearOverlay.width / 2;
                                    const dy = mouse.y + parent.y - linearOverlay.height / 2;
                                    editorPreview.linearMaskRotation = Math.atan2(dy, dx) * 180 / Math.PI + 90;
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            enabled: activeMaskTool === "linear"
                            acceptedButtons: Qt.LeftButton
                            cursorShape: Qt.CrossCursor
                            z: -1
                            onPressed: function(mouse) {
                                editorPreview.linearMaskPosition = clamp(mouse.y / linearOverlay.height, 0, 1);
                            }
                        }
                    }

                    Item {
                        id: radialOverlay
                        visible: editorPreview.radialMaskEnabled
                        x: editorPreview.radialMaskCenterX * parent.width - width / 2
                        y: editorPreview.radialMaskCenterY * parent.height - height / 2
                        width: Math.max(24, parent.width * editorPreview.radialMaskRadiusX * 2)
                        height: Math.max(24, parent.height * editorPreview.radialMaskRadiusY * 2)
                        rotation: editorPreview.radialMaskRotation
                        opacity: editorPreview.radialMaskEnabled ? (activeMaskTool === "radial" ? 0.92 : 0.45) : 0.0

                        Canvas {
                            id: radialCanvas
                            anchors.fill: parent
                            Connections {
                                target: editorPreview

                                function onControlsChanged() {
                                    radialCanvas.requestPaint();
                                }
                            }

                            onPaint: {
                                const ctx = getContext("2d");
                                ctx.reset();
                                ctx.strokeStyle = "#f4f7fa";
                                ctx.lineWidth = 1.5;
                                ctx.fillStyle = "rgba(201,154,82,0.18)";
                                ctx.beginPath();
                                ctx.ellipse(width / 2, height / 2, Math.max(1, width / 2 - 1), Math.max(1, height / 2 - 1), 0, 0, Math.PI * 2);
                                ctx.fill();
                                ctx.stroke();

                                const innerScale = Math.max(0.0, 1.0 - editorPreview.radialMaskFeather);
                                ctx.strokeStyle = "rgba(244,247,250,0.45)";
                                ctx.beginPath();
                                ctx.ellipse(width / 2, height / 2,
                                            Math.max(1, (width / 2 - 1) * innerScale),
                                            Math.max(1, (height / 2 - 1) * innerScale),
                                            0, 0, Math.PI * 2);
                                ctx.stroke();
                            }
                        }

                        Rectangle {
                            id: radialCenterHandle
                            x: radialOverlay.width / 2 - width / 2
                            y: radialOverlay.height / 2 - height / 2
                            width: 16
                            height: 16
                            radius: 8
                            color: "#f4f7fa"
                            border.width: 1
                            border.color: "#101419"

                            MouseArea {
                                anchors.fill: parent
                                enabled: activeMaskTool === "radial"
                                cursorShape: Qt.SizeAllCursor
                                onPressed: root.activeMaskTool = "radial"
                                onPositionChanged: function(mouse) {
                                    if (!pressed)
                                        return;
                                    const previewItem = radialOverlay.parent;
                                    editorPreview.radialMaskCenterX = clamp((radialOverlay.x + radialCenterHandle.x + mouse.x) / previewItem.width, 0, 1);
                                    editorPreview.radialMaskCenterY = clamp((radialOverlay.y + radialCenterHandle.y + mouse.y) / previewItem.height, 0, 1);
                                }
                            }
                        }

                        Rectangle {
                            id: radialWidthHandle
                            x: radialOverlay.width - width / 2
                            y: radialOverlay.height / 2 - height / 2
                            width: 14
                            height: 14
                            radius: 7
                            color: "#8ab8d8"
                            border.width: 1
                            border.color: "#101419"

                            MouseArea {
                                anchors.fill: parent
                                enabled: activeMaskTool === "radial"
                                cursorShape: Qt.SizeHorCursor
                                onPressed: root.activeMaskTool = "radial"
                                onPositionChanged: function(mouse) {
                                    if (!pressed)
                                        return;
                                    editorPreview.radialMaskRadiusX = clamp(
                                                (radialWidthHandle.x + mouse.x) / radialOverlay.parent.width / 2,
                                                0.05,
                                                1.0);
                                }
                            }
                        }

                        Rectangle {
                            id: radialHeightHandle
                            x: radialOverlay.width / 2 - width / 2
                            y: radialOverlay.height - height / 2
                            width: 14
                            height: 14
                            radius: 7
                            color: "#8ab8d8"
                            border.width: 1
                            border.color: "#101419"

                            MouseArea {
                                anchors.fill: parent
                                enabled: activeMaskTool === "radial"
                                cursorShape: Qt.SizeVerCursor
                                onPressed: root.activeMaskTool = "radial"
                                onPositionChanged: function(mouse) {
                                    if (!pressed)
                                        return;
                                    editorPreview.radialMaskRadiusY = clamp(
                                                (radialHeightHandle.y + mouse.y) / radialOverlay.parent.height / 2,
                                                0.05,
                                                1.0);
                                }
                            }
                        }

                        Rectangle {
                            id: radialRotationHandle
                            x: radialOverlay.width / 2 - width / 2
                            y: -30
                            width: 14
                            height: 14
                            radius: 7
                            color: "#c99a52"
                            border.width: 1
                            border.color: "#101419"
                            visible: activeMaskTool === "radial"

                            MouseArea {
                                anchors.fill: parent
                                enabled: activeMaskTool === "radial"
                                cursorShape: Qt.CrossCursor
                                onPressed: root.activeMaskTool = "radial"
                                onPositionChanged: function(mouse) {
                                    if (!pressed)
                                        return;
                                    const dx = mouse.x + parent.x - radialOverlay.width / 2;
                                    const dy = mouse.y + parent.y - radialOverlay.height / 2;
                                    editorPreview.radialMaskRotation = Math.atan2(dy, dx) * 180 / Math.PI + 90;
                                }
                            }
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
                            text: "Mask tools stay attached to the selected asset."
                            color: "#8f98a2"
                            font.family: "Avenir Next"
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }

        BrandPanel {
            Layout.preferredWidth: 380
            Layout.fillHeight: true
            title: "Mask Controls"
            subtitle: "Linear and radial local masks with on-canvas placement."

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: parent.width
                    spacing: 12

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Tools"

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Linear"
                                active: activeMaskTool === "linear"
                                onClicked: ensureToolEnabled("linear")
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Radial"
                                active: activeMaskTool === "radial"
                                onClicked: ensureToolEnabled("radial")
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            ActionButton {
                                Layout.fillWidth: true
                                text: editorPreview.linearMaskEnabled ? "Hide Linear" : "Show Linear"
                                active: editorPreview.linearMaskEnabled
                                onClicked: {
                                    activeMaskTool = "linear";
                                    editorPreview.linearMaskEnabled = !editorPreview.linearMaskEnabled;
                                }
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: editorPreview.radialMaskEnabled ? "Hide Radial" : "Show Radial"
                                active: editorPreview.radialMaskEnabled
                                onClicked: {
                                    activeMaskTool = "radial";
                                    editorPreview.radialMaskEnabled = !editorPreview.radialMaskEnabled;
                                }
                            }
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Adjustment"

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Exposure"
                            valueText: currentMaskExposure().toFixed(2) + " EV"
                            from: -2
                            to: 2
                            value: currentMaskExposure()
                            onValueDragged: {
                                if (activeMaskTool === "radial")
                                    editorPreview.radialMaskExposure = value;
                                else
                                    editorPreview.linearMaskExposure = value;
                            }
                            onResetRequested: {
                                if (activeMaskTool === "radial")
                                    editorPreview.radialMaskExposure = 0;
                                else
                                    editorPreview.linearMaskExposure = 0;
                            }
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Warmth"
                            valueText: Math.round(currentMaskTemperature()).toString()
                            from: -100
                            to: 100
                            value: currentMaskTemperature()
                            onValueDragged: {
                                if (activeMaskTool === "radial")
                                    editorPreview.radialMaskTemperature = value;
                                else
                                    editorPreview.linearMaskTemperature = value;
                            }
                            onResetRequested: {
                                if (activeMaskTool === "radial")
                                    editorPreview.radialMaskTemperature = 0;
                                else
                                    editorPreview.linearMaskTemperature = 0;
                            }
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: activeMaskTool === "radial" ? "Radial Shape" : "Linear Shape"

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: activeMaskTool === "linear"
                            label: "Position"
                            valueText: editorPreview.linearMaskPosition.toFixed(2)
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: editorPreview.linearMaskPosition
                            onValueDragged: editorPreview.linearMaskPosition = value
                            onResetRequested: editorPreview.linearMaskPosition = 0.5
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Feather"
                            valueText: (activeMaskTool === "radial" ? editorPreview.radialMaskFeather : editorPreview.linearMaskFeather).toFixed(2)
                            from: 0.05
                            to: 1.0
                            stepSize: 0.01
                            value: activeMaskTool === "radial" ? editorPreview.radialMaskFeather : editorPreview.linearMaskFeather
                            onValueDragged: {
                                if (activeMaskTool === "radial")
                                    editorPreview.radialMaskFeather = value;
                                else
                                    editorPreview.linearMaskFeather = value;
                            }
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Rotation"
                            valueText: Math.round(activeMaskTool === "radial" ? editorPreview.radialMaskRotation : editorPreview.linearMaskRotation).toString() + "°"
                            from: -180
                            to: 180
                            stepSize: 1
                            value: activeMaskTool === "radial" ? editorPreview.radialMaskRotation : editorPreview.linearMaskRotation
                            onValueDragged: {
                                if (activeMaskTool === "radial")
                                    editorPreview.radialMaskRotation = value;
                                else
                                    editorPreview.linearMaskRotation = value;
                            }
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: activeMaskTool === "radial"
                            label: "Center X"
                            valueText: editorPreview.radialMaskCenterX.toFixed(2)
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: editorPreview.radialMaskCenterX
                            onValueDragged: editorPreview.radialMaskCenterX = value
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: activeMaskTool === "radial"
                            label: "Center Y"
                            valueText: editorPreview.radialMaskCenterY.toFixed(2)
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: editorPreview.radialMaskCenterY
                            onValueDragged: editorPreview.radialMaskCenterY = value
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: activeMaskTool === "radial"
                            label: "Radius X"
                            valueText: editorPreview.radialMaskRadiusX.toFixed(2)
                            from: 0.05
                            to: 1.0
                            stepSize: 0.01
                            value: editorPreview.radialMaskRadiusX
                            onValueDragged: editorPreview.radialMaskRadiusX = value
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: activeMaskTool === "radial"
                            label: "Radius Y"
                            valueText: editorPreview.radialMaskRadiusY.toFixed(2)
                            from: 0.05
                            to: 1.0
                            stepSize: 0.01
                            value: editorPreview.radialMaskRadiusY
                            onValueDragged: editorPreview.radialMaskRadiusY = value
                        }
                    }
                }
            }
        }
    }
}
