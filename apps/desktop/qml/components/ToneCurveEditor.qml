import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property color accent: "#c99a52"
    property real shadowsValue: 0
    property real darksValue: 0
    property real lightsValue: 0
    property real highlightsValue: 0
    signal curveValueDragged(string pointName, real value)

    implicitHeight: 220

    function pointName(index) {
        return ["shadows", "darks", "lights", "highlights"][index];
    }

    function pointXNorm(index) {
        return [0.20, 0.40, 0.65, 0.85][index];
    }

    function pointValue(index) {
        if (index === 0)
            return shadowsValue;
        if (index === 1)
            return darksValue;
        if (index === 2)
            return lightsValue;
        return highlightsValue;
    }

    function pointOutput(index) {
        const baseline = pointXNorm(index);
        return Math.max(0, Math.min(1, baseline + (pointValue(index) / 100.0) * 0.34));
    }

    function pointPosition(index) {
        return Qt.point(curveArea.x + pointXNorm(index) * curveArea.width, curveArea.y + (1.0 - pointOutput(index)) * curveArea.height);
    }

    function valueForY(y) {
        const normalized = Math.max(0, Math.min(1, 1.0 - ((y - curveArea.y) / curveArea.height)));
        const baseline = pointXNorm(dragArea.draggingIndex);
        return Math.max(-100, Math.min(100, ((normalized - baseline) / 0.34) * 100.0));
    }

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: "#101419"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: "Parametric Curve"
                color: "#e8edf2"
                font.family: "Avenir Next"
                font.pixelSize: 12
                font.weight: Font.Medium
            }

            Item {
                Layout.fillWidth: true
            }

            Label {
                text: "Drag points"
                color: "#78828c"
                font.family: "Avenir Next"
                font.pixelSize: 10
            }
        }

        Item {
            id: graphHost
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                id: curveArea
                anchors.fill: parent
                radius: 6
                color: "#0d1116"

                Canvas {
                    id: curveCanvas
                    anchors.fill: parent

                    onPaint: {
                        const ctx = getContext("2d");
                        ctx.reset();

                        ctx.strokeStyle = "#1e2730";
                        ctx.lineWidth = 1;
                        for (let index = 1; index < 4; ++index) {
                            const x = (width / 4) * index;
                            const y = (height / 4) * index;
                            ctx.beginPath();
                            ctx.moveTo(x, 0);
                            ctx.lineTo(x, height);
                            ctx.stroke();
                            ctx.beginPath();
                            ctx.moveTo(0, y);
                            ctx.lineTo(width, y);
                            ctx.stroke();
                        }

                        ctx.strokeStyle = "#3f4953";
                        ctx.beginPath();
                        ctx.moveTo(0, height);
                        ctx.lineTo(width, 0);
                        ctx.stroke();

                        const points = [Qt.point(0, height), root.pointPosition(0), root.pointPosition(1), root.pointPosition(2), root.pointPosition(3), Qt.point(curveArea.width, 0)];

                        ctx.strokeStyle = root.accent;
                        ctx.lineWidth = 2;
                        ctx.beginPath();
                        ctx.moveTo(points[0].x, points[0].y);
                        for (let index = 1; index < points.length; ++index) {
                            ctx.lineTo(points[index].x, points[index].y);
                        }
                        ctx.stroke();
                    }

                    Component.onCompleted: requestPaint()
                }

                Repeater {
                    model: 4

                    delegate: Rectangle {
                        required property int index

                        property point handlePosition: root.pointPosition(index)

                        x: handlePosition.x - width / 2
                        y: handlePosition.y - height / 2
                        width: 14
                        height: 14
                        radius: 7
                        color: root.accent

                        Rectangle {
                            anchors.centerIn: parent
                            width: 4
                            height: 4
                            radius: 2
                            color: "#f0f4f7"
                        }
                    }
                }

                MouseArea {
                    id: dragArea
                    anchors.fill: parent
                    hoverEnabled: true

                    property int draggingIndex: -1

                    function closestHandle(mouseX, mouseY) {
                        let bestIndex = -1;
                        let bestDistance = 24;
                        for (let index = 0; index < 4; ++index) {
                            const position = root.pointPosition(index);
                            const dx = position.x - mouseX;
                            const dy = position.y - mouseY;
                            const distance = Math.sqrt(dx * dx + dy * dy);
                            if (distance < bestDistance) {
                                bestDistance = distance;
                                bestIndex = index;
                            }
                        }
                        return bestIndex;
                    }

                    onPressed: function (mouse) {
                        draggingIndex = closestHandle(mouse.x, mouse.y);
                        if (draggingIndex >= 0) {
                            root.curveValueDragged(root.pointName(draggingIndex), root.valueForY(mouse.y));
                            curveCanvas.requestPaint();
                        }
                    }

                    onPositionChanged: function (mouse) {
                        if (draggingIndex >= 0) {
                            root.curveValueDragged(root.pointName(draggingIndex), root.valueForY(mouse.y));
                            curveCanvas.requestPaint();
                        }
                    }

                    onReleased: draggingIndex = -1
                    onCanceled: draggingIndex = -1
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Repeater {
                model: [
                    {
                        label: "Shadows",
                        value: Math.round(root.shadowsValue)
                    },
                    {
                        label: "Darks",
                        value: Math.round(root.darksValue)
                    },
                    {
                        label: "Lights",
                        value: Math.round(root.lightsValue)
                    },
                    {
                        label: "Highlights",
                        value: Math.round(root.highlightsValue)
                    }
                ]

                delegate: Rectangle {
                    required property var modelData

                    Layout.fillWidth: true
                    radius: 6
                    color: "#141a20"
                    implicitHeight: 36

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 0

                        Label {
                            text: modelData.label
                            color: "#7f8993"
                            font.family: "Avenir Next"
                            font.pixelSize: 9
                        }

                        Label {
                            text: modelData.value
                            color: "#edf1f4"
                            font.family: "Menlo"
                            font.pixelSize: 10
                            font.weight: Font.Medium
                        }
                    }
                }
            }
        }
    }

    onShadowsValueChanged: curveCanvas.requestPaint()
    onDarksValueChanged: curveCanvas.requestPaint()
    onLightsValueChanged: curveCanvas.requestPaint()
    onHighlightsValueChanged: curveCanvas.requestPaint()
}
