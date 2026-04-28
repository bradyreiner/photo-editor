import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property color accent: theme.accentWarm400
    property string selectedChannel: "Master"
    property var pointsModel: []
    property var availableChannels: ["Master", "Red", "Green", "Blue"]
    readonly property real graphInset: 16
    readonly property real handleHitRadius: 28
    readonly property real endpointHandleHitRadius: 36
    signal pointsEdited(var points)
    signal channelSelected(string channel)

    Theme { id: theme }

    implicitHeight: 292

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
        points[0] = { x: 0.0, y: points[0].y };
        points[points.length - 1] = { x: 1.0, y: points[points.length - 1].y };
        return points;
    }

    function graphPoint(index) {
        const point = currentPoints()[index];
        const graphWidth = Math.max(1, curveArea.width - root.graphInset * 2);
        const graphHeight = Math.max(1, curveArea.height - root.graphInset * 2);
        return Qt.point(
            curveArea.x + root.graphInset + point.x * graphWidth,
            curveArea.y + root.graphInset + (1.0 - point.y) * graphHeight
        );
    }

    function closestHandle(mouseX, mouseY) {
        const points = currentPoints();
        let bestIndex = -1;
        let bestDistance = Number.POSITIVE_INFINITY;
        for (let index = 0; index < points.length; ++index) {
            const graph = graphPoint(index);
            const dx = graph.x - mouseX;
            const dy = graph.y - mouseY;
            const distance = Math.sqrt(dx * dx + dy * dy);
            const threshold = (index === 0 || index === points.length - 1)
                    ? root.endpointHandleHitRadius
                    : root.handleHitRadius;
            if (distance <= threshold && distance < bestDistance) {
                bestDistance = distance;
                bestIndex = index;
            }
        }
        return bestIndex;
    }

    function normalizeForGraph(mouseX, mouseY) {
        const graphWidth = Math.max(1, curveArea.width - root.graphInset * 2);
        const graphHeight = Math.max(1, curveArea.height - root.graphInset * 2);
        return {
            x: clampUnit((mouseX - curveArea.x - root.graphInset) / graphWidth),
            y: clampUnit(1.0 - ((mouseY - curveArea.y - root.graphInset) / graphHeight))
        };
    }

    function updatePoint(index, mouseX, mouseY) {
        const points = currentPoints();
        if (index < 0 || index >= points.length)
            return;

        const normalized = normalizeForGraph(mouseX, mouseY);
        if (index === 0) {
            points[index] = { x: 0.0, y: normalized.y };
        } else if (index === points.length - 1) {
            points[index] = { x: 1.0, y: normalized.y };
        } else {
            const minimumX = points[index - 1].x + 0.01;
            const maximumX = points[index + 1].x - 0.01;
            points[index] = {
                x: Math.max(minimumX, Math.min(maximumX, normalized.x)),
                y: normalized.y
            };
        }
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
        radius: 0
        color: theme.alpha(theme.glassBase, 0.18)
        border.width: 1
        border.color: theme.alpha(theme.glassEdge, 0.12)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.space12
        spacing: theme.space10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: "Point Curve"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 13
                font.weight: Font.Bold
            }

            Item {
                Layout.fillWidth: true
            }

            Label {
                text: "IN / OUT"
                color: theme.textFaint
                font.family: theme.fontMono
                font.pixelSize: 9
                font.letterSpacing: 1.0
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Repeater {
                model: root.availableChannels

                delegate: Rectangle {
                    required property var modelData

                    Layout.fillWidth: true
                    Layout.preferredHeight: 25
                    radius: 0
                    color: root.selectedChannel === modelData ? theme.alpha(root.accent, 0.16) : theme.alpha(theme.ink950, 0.28)
                    border.width: 1
                    border.color: root.selectedChannel === modelData ? theme.alpha(root.accent, 0.56) : theme.alpha(theme.glassEdge, 0.16)

                    Label {
                        anchors.centerIn: parent
                        text: modelData.toUpperCase()
                        color: root.selectedChannel === modelData ? theme.textStrong : theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.8
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
                radius: 0
                color: theme.alpha(theme.ink950, 0.68)
                border.width: 1
                border.color: theme.alpha(theme.polar, 0.10)

                Canvas {
                    id: curveCanvas
                    anchors.fill: parent
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()

                    onPaint: {
                        const ctx = getContext("2d");
                        ctx.reset();
                        const graphLeft = root.graphInset;
                        const graphTop = root.graphInset;
                        const graphWidth = Math.max(1, width - root.graphInset * 2);
                        const graphHeight = Math.max(1, height - root.graphInset * 2);
                        const graphRight = graphLeft + graphWidth;
                        const graphBottom = graphTop + graphHeight;

                        ctx.lineWidth = 1;
                        ctx.strokeStyle = theme.alpha(theme.polar, 0.055);
                        for (let index = 1; index < 8; ++index) {
                            const x = graphLeft + (graphWidth / 8) * index;
                            const y = graphTop + (graphHeight / 8) * index;
                            ctx.beginPath();
                            ctx.moveTo(x, graphTop);
                            ctx.lineTo(x, graphBottom);
                            ctx.stroke();
                            ctx.beginPath();
                            ctx.moveTo(graphLeft, y);
                            ctx.lineTo(graphRight, y);
                            ctx.stroke();
                        }

                        ctx.strokeStyle = theme.alpha(theme.polar, 0.14);
                        for (let index = 1; index < 4; ++index) {
                            const x = graphLeft + (graphWidth / 4) * index;
                            const y = graphTop + (graphHeight / 4) * index;
                            ctx.beginPath();
                            ctx.moveTo(x, graphTop);
                            ctx.lineTo(x, graphBottom);
                            ctx.stroke();
                            ctx.beginPath();
                            ctx.moveTo(graphLeft, y);
                            ctx.lineTo(graphRight, y);
                            ctx.stroke();
                        }

                        ctx.strokeStyle = theme.alpha(theme.polar, 0.24);
                        ctx.beginPath();
                        ctx.moveTo(graphLeft, graphBottom);
                        ctx.lineTo(graphRight, graphTop);
                        ctx.stroke();

                        ctx.strokeStyle = theme.alpha(theme.polar, 0.28);
                        ctx.strokeRect(graphLeft, graphTop, graphWidth, graphHeight);

                        const samples = root.sampledCurvePoints();
                        if (samples.length > 0) {
                            ctx.strokeStyle = root.accent;
                            ctx.lineWidth = 2.4;
                            ctx.lineJoin = "round";
                            ctx.lineCap = "round";
                            ctx.beginPath();
                            ctx.moveTo(
                                graphLeft + samples[0].x * graphWidth,
                                graphTop + (1.0 - samples[0].y) * graphHeight
                            );
                            for (let index = 1; index < samples.length; ++index) {
                                ctx.lineTo(
                                    graphLeft + samples[index].x * graphWidth,
                                    graphTop + (1.0 - samples[index].y) * graphHeight
                                );
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
                        width: index === 0 || index === root.currentPoints().length - 1
                               ? (index === interaction.selectedIndex ? 15 : 13)
                               : (index === interaction.selectedIndex ? 13 : 11)
                        height: width
                        radius: 1
                        color: index === 0 || index === root.currentPoints().length - 1 ? theme.alpha(theme.polar, 0.94) : root.accent
                        border.width: 1
                        border.color: index === interaction.selectedIndex ? theme.polar : theme.alpha(theme.ink950, 0.8)

                        Rectangle {
                            anchors.centerIn: parent
                            width: 3
                            height: 3
                            radius: 0
                            color: index === 0 || index === root.currentPoints().length - 1 ? theme.ink950 : theme.polar
                        }
                    }
                }

                Label {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 4
                    anchors.bottomMargin: 3
                    text: "0"
                    color: theme.alpha(theme.textFaint, 0.78)
                    font.family: theme.fontMono
                    font.pixelSize: 8
                }

                Label {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.rightMargin: 4
                    anchors.topMargin: 3
                    text: "255"
                    color: theme.alpha(theme.textFaint, 0.78)
                    font.family: theme.fontMono
                    font.pixelSize: 8
                }

                MouseArea {
                    id: interaction
                    anchors.fill: parent
                    hoverEnabled: true
                    preventStealing: true
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.CrossCursor

                    property int draggingIndex: -1
                    property int selectedIndex: -1

                    onPressed: function (mouse) {
                        const handleIndex = root.closestHandle(mouse.x, mouse.y);
                        if (handleIndex >= 0) {
                            draggingIndex = handleIndex;
                            selectedIndex = handleIndex;
                            root.updatePoint(handleIndex, mouse.x, mouse.y);
                        } else {
                            const insertedIndex = root.insertPoint(mouse.x, mouse.y);
                            draggingIndex = insertedIndex;
                            selectedIndex = insertedIndex;
                        }
                        curveCanvas.requestPaint();
                    }

                    onPositionChanged: function (mouse) {
                        if (draggingIndex < 0 || draggingIndex >= root.currentPoints().length)
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
            implicitHeight: 32
            radius: 0
            color: theme.alpha(theme.ink950, 0.34)
            border.width: 1
            border.color: theme.alpha(theme.glassEdge, 0.12)

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Label {
                    text: {
                        const points = root.currentPoints();
                        if (interaction.selectedIndex === 0)
                            return "Black Point";
                        if (interaction.selectedIndex === points.length - 1)
                            return "White Point";
                        if (interaction.selectedIndex > 0 && interaction.selectedIndex < points.length - 1)
                            return "Point " + interaction.selectedIndex;
                        return "Drag endpoints for black and white points";
                    }
                    color: theme.textFaint
                    font.family: theme.fontMono
                    font.pixelSize: 9
                    font.letterSpacing: 0.4
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
                    color: theme.textStrong
                    font.family: theme.fontMono
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }
            }
        }
    }

    onPointsModelChanged: curveCanvas.requestPaint()
    onSelectedChannelChanged: curveCanvas.requestPaint()
}
