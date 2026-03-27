import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property color accent: "#c99a52"
    property string selectedChannel: "Master"
    property var pointsModel: []
    property var availableChannels: ["Master", "Red", "Green", "Blue"]
    signal pointsEdited(var points)
    signal channelSelected(string channel)

    implicitHeight: 260

    function clampUnit(value) {
        return Math.max(0, Math.min(1, value));
    }

    function defaultPoints() {
        return [
            { x: 0.0, y: 0.0 },
            { x: 1.0, y: 1.0 }
        ];
    }

    function currentPoints() {
        const source = pointsModel && pointsModel.length >= 2 ? pointsModel : defaultPoints();
        const points = [];
        for (let index = 0; index < source.length; ++index) {
            points.push({
                x: clampUnit(source[index].x),
                y: clampUnit(source[index].y)
            });
        }
        points.sort(function (left, right) {
            return left.x - right.x;
        });
        points[0] = { x: 0.0, y: 0.0 };
        points[points.length - 1] = { x: 1.0, y: 1.0 };
        return points;
    }

    function graphPoint(index) {
        const point = currentPoints()[index];
        return Qt.point(curveArea.x + point.x * curveArea.width, curveArea.y + (1.0 - point.y) * curveArea.height);
    }

    function closestHandle(mouseX, mouseY) {
        const points = currentPoints();
        let bestIndex = -1;
        let bestDistance = 20;
        for (let index = 0; index < points.length; ++index) {
            const graph = graphPoint(index);
            const dx = graph.x - mouseX;
            const dy = graph.y - mouseY;
            const distance = Math.sqrt(dx * dx + dy * dy);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestIndex = index;
            }
        }
        return bestIndex;
    }

    function normalizeForGraph(mouseX, mouseY) {
        return {
            x: clampUnit((mouseX - curveArea.x) / Math.max(1, curveArea.width)),
            y: clampUnit(1.0 - ((mouseY - curveArea.y) / Math.max(1, curveArea.height)))
        };
    }

    function updatePoint(index, mouseX, mouseY) {
        const points = currentPoints();
        if (index <= 0 || index >= points.length - 1)
            return;

        const normalized = normalizeForGraph(mouseX, mouseY);
        const minimumX = points[index - 1].x + 0.01;
        const maximumX = points[index + 1].x - 0.01;
        points[index] = {
            x: Math.max(minimumX, Math.min(maximumX, normalized.x)),
            y: normalized.y
        };
        pointsEdited(points);
    }

    function insertPoint(mouseX, mouseY) {
        const points = currentPoints();
        const normalized = normalizeForGraph(mouseX, mouseY);
        let insertIndex = points.length - 1;
        for (let index = 1; index < points.length; ++index) {
            if (normalized.x < points[index].x) {
                insertIndex = index;
                break;
            }
        }
        points.splice(insertIndex, 0, normalized);
        pointsEdited(points);
        return insertIndex;
    }

    function removePoint(index) {
        const points = currentPoints();
        if (index <= 0 || index >= points.length - 1)
            return;
        points.splice(index, 1);
        pointsEdited(points);
    }

    function sampledCurvePoints() {
        const points = currentPoints();
        if (points.length < 2)
            return defaultPoints();

        const secants = [];
        const tangents = [];
        for (let index = 0; index < points.length - 1; ++index) {
            const span = Math.max(0.0001, points[index + 1].x - points[index].x);
            secants.push((points[index + 1].y - points[index].y) / span);
        }

        tangents.push(secants[0]);
        for (let index = 1; index < points.length - 1; ++index) {
            if (secants[index - 1] * secants[index] <= 0)
                tangents.push(0);
            else
                tangents.push((secants[index - 1] + secants[index]) * 0.5);
        }
        tangents.push(secants[secants.length - 1]);

        for (let index = 0; index < secants.length; ++index) {
            if (Math.abs(secants[index]) <= 0.000001) {
                tangents[index] = 0;
                tangents[index + 1] = 0;
                continue;
            }

            const a = tangents[index] / secants[index];
            const b = tangents[index + 1] / secants[index];
            const magnitude = Math.sqrt(a * a + b * b);
            if (magnitude <= 3.0)
                continue;

            const scale = 3.0 / magnitude;
            tangents[index] = scale * a * secants[index];
            tangents[index + 1] = scale * b * secants[index];
        }

        const samples = [];
        for (let index = 0; index < points.length - 1; ++index) {
            const start = points[index];
            const end = points[index + 1];
            const span = Math.max(0.0001, end.x - start.x);
            const sampleCount = Math.max(8, Math.round(span * 120));
            for (let sample = 0; sample < sampleCount; ++sample) {
                const t = sample / sampleCount;
                const t2 = t * t;
                const t3 = t2 * t;
                const y = (2 * t3 - 3 * t2 + 1) * start.y
                        + (t3 - 2 * t2 + t) * span * tangents[index]
                        + (-2 * t3 + 3 * t2) * end.y
                        + (t3 - t2) * span * tangents[index + 1];
                samples.push({
                    x: start.x + span * t,
                    y: clampUnit(y)
                });
            }
        }
        samples.push(points[points.length - 1]);
        return samples;
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
                text: "Point Curve"
                color: "#e8edf2"
                font.family: "Avenir Next"
                font.pixelSize: 12
                font.weight: Font.Medium
            }

            Item {
                Layout.fillWidth: true
            }

            Label {
                text: "Click to add, drag to shape, double-click to remove"
                color: "#78828c"
                font.family: "Avenir Next"
                font.pixelSize: 10
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Repeater {
                model: root.availableChannels

                delegate: Rectangle {
                    required property var modelData

                    Layout.preferredWidth: 54
                    Layout.preferredHeight: 26
                    radius: 7
                    color: root.selectedChannel === modelData ? root.accent : "#151b22"

                    Label {
                        anchors.centerIn: parent
                        text: modelData
                        color: root.selectedChannel === modelData ? "#0c1014" : "#dbe2e8"
                        font.family: "Avenir Next"
                        font.pixelSize: 10
                        font.weight: Font.Medium
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.channelSelected(modelData)
                    }
                }
            }
        }

        Item {
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

                        const samples = root.sampledCurvePoints();
                        if (samples.length > 0) {
                            ctx.strokeStyle = root.accent;
                            ctx.lineWidth = 2;
                            ctx.beginPath();
                            ctx.moveTo(samples[0].x * width, (1.0 - samples[0].y) * height);
                            for (let index = 1; index < samples.length; ++index) {
                                ctx.lineTo(samples[index].x * width, (1.0 - samples[index].y) * height);
                            }
                            ctx.stroke();
                        }
                    }

                    Component.onCompleted: requestPaint()
                }

                Repeater {
                    model: root.currentPoints()

                    delegate: Rectangle {
                        required property var modelData
                        required property int index

                        property point handlePoint: root.graphPoint(index)

                        x: handlePoint.x - width / 2
                        y: handlePoint.y - height / 2
                        width: index === interaction.selectedIndex ? 16 : 14
                        height: width
                        radius: width / 2
                        color: index === 0 || index === root.currentPoints().length - 1 ? "#d7dde4" : root.accent
                        border.width: index === interaction.selectedIndex ? 2 : 0
                        border.color: "#f4f7f9"

                        Rectangle {
                            anchors.centerIn: parent
                            width: 4
                            height: 4
                            radius: 2
                            color: index === 0 || index === root.currentPoints().length - 1 ? "#0d1116" : "#f0f4f7"
                        }
                    }
                }

                MouseArea {
                    id: interaction
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.CrossCursor

                    property int draggingIndex: -1
                    property int selectedIndex: -1

                    onPressed: function (mouse) {
                        const handleIndex = root.closestHandle(mouse.x, mouse.y);
                        if (handleIndex >= 0) {
                            draggingIndex = handleIndex;
                            selectedIndex = handleIndex;
                            if (handleIndex > 0 && handleIndex < root.currentPoints().length - 1)
                                root.updatePoint(handleIndex, mouse.x, mouse.y);
                        } else {
                            const insertedIndex = root.insertPoint(mouse.x, mouse.y);
                            draggingIndex = insertedIndex;
                            selectedIndex = insertedIndex;
                        }
                        curveCanvas.requestPaint();
                    }

                    onPositionChanged: function (mouse) {
                        if (draggingIndex <= 0 || draggingIndex >= root.currentPoints().length - 1)
                            return;
                        root.updatePoint(draggingIndex, mouse.x, mouse.y);
                        curveCanvas.requestPaint();
                    }

                    onDoubleClicked: function (mouse) {
                        const handleIndex = root.closestHandle(mouse.x, mouse.y);
                        if (handleIndex > 0 && handleIndex < root.currentPoints().length - 1) {
                            root.removePoint(handleIndex);
                            selectedIndex = -1;
                            draggingIndex = -1;
                            curveCanvas.requestPaint();
                        }
                    }

                    onReleased: draggingIndex = -1
                    onCanceled: draggingIndex = -1
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 34
            radius: 6
            color: "#141a20"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Label {
                    text: interaction.selectedIndex > 0 && interaction.selectedIndex < root.currentPoints().length - 1
                          ? "Point " + interaction.selectedIndex
                          : "Endpoints locked"
                    color: "#7f8993"
                    font.family: "Avenir Next"
                    font.pixelSize: 9
                }

                Item {
                    Layout.fillWidth: true
                }

                Label {
                    text: {
                        const points = root.currentPoints();
                        const index = Math.max(0, Math.min(points.length - 1, interaction.selectedIndex >= 0 ? interaction.selectedIndex : 0));
                        return "In " + Math.round(points[index].x * 255) + "  Out " + Math.round(points[index].y * 255);
                    }
                    color: "#edf1f4"
                    font.family: "Menlo"
                    font.pixelSize: 10
                }
            }
        }
    }

    onPointsModelChanged: curveCanvas.requestPaint()
    onSelectedChannelChanged: curveCanvas.requestPaint()
}
