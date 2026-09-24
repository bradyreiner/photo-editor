import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    property color accent: theme.accentWarm400
    property string selectedChannel: "RGB"
    property string curveMode: "Point"
    property var pointsModel: []
    property var availableChannels: ["RGB", "Luma", "Red", "Green", "Blue"]
    property var presetNames: []
    property var histogramRedData: []
    property var histogramGreenData: []
    property var histogramBlueData: []
    property var histogramLuminanceData: []
    property var presetPreviewProvider: null
    property var previewPointsModel: []
    property bool histogramLogScale: true
    property bool compactMode: false
    property bool expanded: false
    readonly property bool compactLayout: compactMode && !expanded
    readonly property real histogramPrimaryFillOpacity: 0.10
    readonly property real histogramPrimaryStrokeOpacity: 0.28
    readonly property real histogramLuminanceFillOpacity: 0.11
    readonly property real histogramLuminanceStrokeOpacity: 0.30
    readonly property real histogramRgbFillOpacity: 0.035
    readonly property real histogramRgbStrokeOpacity: 0.12
    readonly property real graphInset: compactLayout ? 10 : 16
    readonly property real handleHitRadius: 28
    readonly property real endpointHandleHitRadius: 36
    readonly property int pointCount: currentPoints().length
    readonly property bool selectedPointDeletable: effectiveSelectedIndex() > 0 && effectiveSelectedIndex() < pointCount - 1
    readonly property string channelModeLabel: selectedChannel + " " + curveMode
    signal editStarted()
    signal editFinished()
    signal pointsEdited(var points)
    signal channelSelected(string channel)
    signal presetSelected(string preset)

    Theme { id: theme }

    activeFocusOnTab: true
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Point curve editor")
    Accessible.description: qsTr("Add points by clicking the graph. Use arrows to adjust the selected point, Shift for fine adjustment, and Delete to remove it.")
    implicitHeight: compactLayout ? 208 : 430

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape && interaction.gestureActive) {
            interaction.finishGesture(true);
            event.accepted = true;
            return;
        }
        if (interaction.gestureActive)
            return;
        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            root.editStarted();
            root.deleteSelectedPoint();
            root.editFinished();
            event.accepted = true;
            return;
        }
        const dx = event.key === Qt.Key_Right ? 1 : event.key === Qt.Key_Left ? -1 : 0;
        const dy = event.key === Qt.Key_Up ? 1 : event.key === Qt.Key_Down ? -1 : 0;
        if (dx === 0 && dy === 0)
            return;
        const step = (event.modifiers & Qt.ShiftModifier) !== 0 ? 0.1 : 1;
        const point = currentPoints()[effectiveSelectedIndex()];
        root.editStarted();
        updateSelectedNumeric(dx !== 0 ? "x" : "y",
                              String((dx !== 0 ? point.x : point.y) * 255 + (dx + dy) * step));
        root.editFinished();
        event.accepted = true;
    }

    function clampUnit(value) {
        return Math.max(0, Math.min(1, value));
    }

    function defaultPoints() {
        return [
            { x: 0.0, y: 0.0 },
            { x: 1.0, y: 1.0 }
        ];
    }

    function normalizedPoints(sourceModel) {
        const source = sourceModel && sourceModel.length >= 2 ? sourceModel : defaultPoints();
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

    function currentPoints() {
        return normalizedPoints(pointsModel);
    }

    function displayPoints() {
        return previewPointsModel && previewPointsModel.length >= 2
                ? normalizedPoints(previewPointsModel)
                : currentPoints();
    }

    function graphPoint(index) {
        const point = displayPoints()[index];
        const graphWidth = Math.max(1, curveArea.width - root.graphInset * 2);
        const graphHeight = Math.max(1, curveArea.height - root.graphInset * 2);
        return Qt.point(
            curveArea.x + root.graphInset + point.x * graphWidth,
            curveArea.y + root.graphInset + (1.0 - point.y) * graphHeight
        );
    }

    function closestHandle(mouseX, mouseY) {
        const points = displayPoints();
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
        // Start a new handle on the existing curve. Clicking above or below
        // the graph must not replace the tonal response before the drag begins.
        normalized.x = Math.max(points[insertIndex - 1].x + 0.01,
                                Math.min(points[insertIndex].x - 0.01, normalized.x));
        if (points[insertIndex].x - points[insertIndex - 1].x < 0.02)
            return -1;
        const samples = sampledCurvePoints();
        for (let index = 1; index < samples.length; ++index) {
            if (samples[index].x < normalized.x)
                continue;
            const left = samples[index - 1];
            const right = samples[index];
            const t = (normalized.x - left.x) / Math.max(0.000001, right.x - left.x);
            normalized.y = left.y + t * (right.y - left.y);
            break;
        }
        points.splice(insertIndex, 0, normalized);
        pointsEdited(points);
        return insertIndex;
    }

    function removePoint(index) {
        const points = currentPoints();
        if (index <= 0 || index >= points.length - 1)
            return false;
        points.splice(index, 1);
        pointsEdited(points);
        return true;
    }

    function deleteSelectedPoint() {
        const index = effectiveSelectedIndex();
        if (!removePoint(index))
            return false;
        interaction.selectedIndex = Math.min(index, Math.max(0, currentPoints().length - 2));
        interaction.draggingIndex = -1;
        curveCanvas.requestPaint();
        return true;
    }

    function resetActiveCurve() {
        interaction.selectedIndex = -1;
        interaction.draggingIndex = -1;
        pointsEdited(defaultPoints());
        curveCanvas.requestPaint();
    }

    function testSelectPoint(index) {
        interaction.selectedIndex = Math.max(-1, Math.min(index, currentPoints().length - 1));
        return interaction.selectedIndex;
    }

    function testDeleteSelectedPoint() {
        return deleteSelectedPoint();
    }

    function testResetActiveCurve() {
        resetActiveCurve();
        return true;
    }

    function effectiveSelectedIndex() {
        const points = currentPoints();
        if (interaction.selectedIndex >= 0 && interaction.selectedIndex < points.length)
            return interaction.selectedIndex;
        return 0;
    }

    function selectedInputText() {
        const points = currentPoints();
        const index = effectiveSelectedIndex();
        if (index < 0 || index >= points.length)
            return "0";
        return Number((points[index].x * 255).toFixed(3)).toString();
    }

    function selectedOutputText() {
        const points = currentPoints();
        const index = effectiveSelectedIndex();
        if (index < 0 || index >= points.length)
            return "0";
        return Number((points[index].y * 255).toFixed(3)).toString();
    }

    function updateSelectedNumeric(axis, text) {
        const points = currentPoints();
        const index = effectiveSelectedIndex();
        const parsed = Number.parseFloat(text);
        if (index < 0 || index >= points.length || !Number.isFinite(parsed))
            return;

        const value = clampUnit(parsed / 255.0);
        const updated = {
            x: points[index].x,
            y: points[index].y
        };
        if (axis === "x" && index > 0 && index < points.length - 1) {
            updated.x = Math.max(points[index - 1].x + 0.01, Math.min(points[index + 1].x - 0.01, value));
        } else if (axis === "y") {
            updated.y = value;
        }

        if (updated.x === points[index].x && updated.y === points[index].y)
            return;
        points[index] = updated;
        pointsEdited(points);
    }

    function sampledCurvePoints() {
        const points = displayPoints();
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

    function previewPreset(preset) {
        if (typeof presetPreviewProvider !== "function")
            return false;
        const previewPoints = presetPreviewProvider(preset);
        if (!previewPoints || previewPoints.length < 2)
            return false;
        previewPointsModel = previewPoints;
        curveCanvas.requestPaint();
        return true;
    }

    function clearPresetPreview() {
        if (!previewPointsModel || previewPointsModel.length === 0)
            return false;
        previewPointsModel = [];
        curveCanvas.requestPaint();
        return true;
    }

    function testPreviewPreset(preset) {
        return previewPreset(preset);
    }

    function testClearPresetPreview() {
        return clearPresetPreview();
    }

    Rectangle {
        anchors.fill: parent
        radius: 0
        color: "transparent"
        border.width: 0
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.space6
        anchors.rightMargin: theme.space6
        anchors.topMargin: theme.space6
        anchors.bottomMargin: theme.space4
        spacing: theme.space6

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: !root.compactLayout

            Label {
                objectName: root.objectName.length > 0 ? root.objectName + "TitleLabel" : ""
                text: "Point Curve"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeTitle
                font.weight: Font.DemiBold
            }

            Item {
                Layout.fillWidth: true
            }

            Label {
                objectName: root.objectName.length > 0 ? root.objectName + "ValueHeaderLabel" : ""
                text: "In / Out"
                color: theme.textFaint
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                font.letterSpacing: 0
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Repeater {
                model: root.availableChannels

                delegate: ToneCurveChannelButton {
                    required property var modelData

                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    channelName: String(modelData)
                    selected: root.selectedChannel === modelData
                    accent: root.accent
                    onSelectedRequested: function(channelName) { root.channelSelected(channelName) }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: !root.compactLayout

            Rectangle {
                objectName: root.objectName.length > 0 ? root.objectName + "ChannelModeBadge" : ""
                Layout.preferredHeight: 22
                Layout.preferredWidth: 112
                radius: 3
                color: theme.alpha(root.accent, 0.08)
                border.width: 1
                border.color: theme.alpha(root.accent, 0.34)

                Label {
                    anchors.centerIn: parent
                    text: root.channelModeLabel
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0
                }
            }

            Rectangle {
                objectName: root.objectName.length > 0 ? root.objectName + "PointCountBadge" : ""
                Layout.preferredHeight: 26
                Layout.preferredWidth: 72
                radius: 3
                color: theme.alpha(theme.ink950, 0.30)
                border.width: 1
                border.color: theme.alpha(theme.glassEdge, 0.16)

                Label {
                    anchors.centerIn: parent
                    text: root.pointCount + " pts"
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0
                }
            }

            Item {
                Layout.fillWidth: true
            }

            ActionButton {
                objectName: root.objectName.length > 0 ? root.objectName + "DeletePointButton" : ""
                Layout.preferredHeight: 28
                Layout.preferredWidth: 70
                enabled: root.selectedPointDeletable
                text: "Delete"
                font.pixelSize: theme.typeCaption
                compact: true
                tone: "danger"
                tooltipText: "Delete the selected curve point."
                onClicked: root.deleteSelectedPoint()
            }

            ActionButton {
                objectName: root.objectName.length > 0 ? root.objectName + "ResetCurveButton" : ""
                Layout.preferredHeight: 28
                Layout.preferredWidth: 58
                text: "Reset"
                font.pixelSize: theme.typeCaption
                compact: true
                tooltipText: "Reset the active curve."
                onClicked: root.resetActiveCurve()
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 6
            rowSpacing: 6
            visible: !root.compactLayout && root.presetNames && root.presetNames.length > 0

            Repeater {
                model: root.presetNames

                delegate: ToneCurvePresetButton {
                    required property var modelData

                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    presetName: String(modelData)
                    onPreviewRequested: function(presetName) { root.previewPreset(presetName) }
                    onPreviewCleared: root.clearPresetPreview()
                    onSelectedRequested: function(presetName) { root.presetSelected(presetName) }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                id: curveArea
                objectName: "toneCurveGraph"
                anchors.fill: parent
                radius: theme.radius8
                color: theme.surfaceInset
                border.width: 1
                border.color: theme.lineSoft
                antialiasing: true

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

                        drawHistogramBackdrop(ctx, graphLeft, graphTop, graphWidth, graphHeight);

                        ctx.lineWidth = 1;
                        ctx.strokeStyle = theme.alpha(theme.polar, 0.045);
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

                        ctx.strokeStyle = theme.alpha(theme.polar, 0.10);
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

                        ctx.strokeStyle = theme.alpha(theme.polar, 0.14);
                        ctx.beginPath();
                        ctx.moveTo(graphLeft, graphBottom);
                        ctx.lineTo(graphRight, graphTop);
                        ctx.stroke();

                        ctx.strokeStyle = theme.alpha(theme.polar, 0.13);
                        ctx.strokeRect(graphLeft, graphTop, graphWidth, graphHeight);

                        const samples = root.sampledCurvePoints();
                        if (samples.length > 0) {
                            const fillGradient = ctx.createLinearGradient(0, graphTop, 0, graphBottom);
                            fillGradient.addColorStop(0, theme.alpha(root.accent, 0.18));
                            fillGradient.addColorStop(1, theme.alpha(root.accent, 0.01));
                            ctx.fillStyle = fillGradient;
                            ctx.beginPath();
                            ctx.moveTo(graphLeft + samples[0].x * graphWidth, graphBottom);
                            for (let index = 0; index < samples.length; ++index) {
                                ctx.lineTo(
                                    graphLeft + samples[index].x * graphWidth,
                                    graphTop + (1.0 - samples[index].y) * graphHeight
                                );
                            }
                            ctx.lineTo(graphLeft + samples[samples.length - 1].x * graphWidth, graphBottom);
                            ctx.closePath();
                            ctx.fill();

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

                    function plotHistogramValue(value) {
                        const v = Math.max(0, value || 0);
                        if (!root.histogramLogScale)
                            return Math.min(1, v);
                        return Math.log(1 + v * 31) / Math.log(32);
                    }

                    function drawHistogramChannel(ctx, data, graphLeft, graphTop, graphWidth, graphHeight, fillColor, strokeColor) {
                        if (!data || data.length !== 256)
                            return;

                        ctx.save();
                        ctx.beginPath();
                        ctx.moveTo(graphLeft, graphTop + graphHeight);
                        const step = graphWidth / 255;
                        for (let index = 0; index < 256; ++index) {
                            const value = plotHistogramValue(data[index]);
                            ctx.lineTo(graphLeft + index * step, graphTop + graphHeight - value * graphHeight);
                        }
                        ctx.lineTo(graphLeft + graphWidth, graphTop + graphHeight);
                        ctx.closePath();
                        ctx.fillStyle = fillColor;
                        ctx.fill();

                        ctx.beginPath();
                        for (let strokeIndex = 0; strokeIndex < 256; ++strokeIndex) {
                            const strokeValue = plotHistogramValue(data[strokeIndex]);
                            const x = graphLeft + strokeIndex * step;
                            const y = graphTop + graphHeight - strokeValue * graphHeight;
                            if (strokeIndex === 0)
                                ctx.moveTo(x, y);
                            else
                                ctx.lineTo(x, y);
                        }
                        ctx.strokeStyle = strokeColor;
                        ctx.lineWidth = 1;
                        ctx.stroke();
                        ctx.restore();
                    }

                    function drawHistogramBackdrop(ctx, graphLeft, graphTop, graphWidth, graphHeight) {
                        if (root.selectedChannel === "Red") {
                            drawHistogramChannel(ctx, root.histogramRedData, graphLeft, graphTop, graphWidth, graphHeight,
                                                 Qt.rgba(1.0, 0.22, 0.18, root.histogramPrimaryFillOpacity),
                                                 Qt.rgba(1.0, 0.22, 0.18, root.histogramPrimaryStrokeOpacity));
                            return;
                        }
                        if (root.selectedChannel === "Green") {
                            drawHistogramChannel(ctx, root.histogramGreenData, graphLeft, graphTop, graphWidth, graphHeight,
                                                 Qt.rgba(0.22, 1.0, 0.68, root.histogramPrimaryFillOpacity),
                                                 Qt.rgba(0.22, 1.0, 0.68, root.histogramPrimaryStrokeOpacity));
                            return;
                        }
                        if (root.selectedChannel === "Blue") {
                            drawHistogramChannel(ctx, root.histogramBlueData, graphLeft, graphTop, graphWidth, graphHeight,
                                                 Qt.rgba(0.0, 0.76, 1.0, root.histogramPrimaryFillOpacity),
                                                 Qt.rgba(0.0, 0.76, 1.0, root.histogramPrimaryStrokeOpacity));
                            return;
                        }

                        drawHistogramChannel(ctx, root.histogramLuminanceData, graphLeft, graphTop, graphWidth, graphHeight,
                                             Qt.rgba(0.86, 0.86, 0.84, root.histogramLuminanceFillOpacity),
                                             Qt.rgba(0.86, 0.86, 0.84, root.histogramLuminanceStrokeOpacity));
                        if (root.selectedChannel === "RGB") {
                            drawHistogramChannel(ctx, root.histogramBlueData, graphLeft, graphTop, graphWidth, graphHeight,
                                                 Qt.rgba(0.0, 0.76, 1.0, root.histogramRgbFillOpacity),
                                                 Qt.rgba(0.0, 0.76, 1.0, root.histogramRgbStrokeOpacity));
                            drawHistogramChannel(ctx, root.histogramGreenData, graphLeft, graphTop, graphWidth, graphHeight,
                                                 Qt.rgba(0.22, 1.0, 0.68, root.histogramRgbFillOpacity),
                                                 Qt.rgba(0.22, 1.0, 0.68, root.histogramRgbStrokeOpacity));
                            drawHistogramChannel(ctx, root.histogramRedData, graphLeft, graphTop, graphWidth, graphHeight,
                                                 Qt.rgba(1.0, 0.22, 0.18, root.histogramRgbFillOpacity),
                                                 Qt.rgba(1.0, 0.22, 0.18, root.histogramRgbStrokeOpacity));
                        }
                    }

                    Component.onCompleted: requestPaint()
                }

                Repeater {
                    model: root.displayPoints()

                    delegate: ToneCurvePointHandle {
                        required property var modelData
                        required property int index

                        property point handlePoint: root.graphPoint(index)
                        readonly property bool endpointHandle: index === 0 || index === root.currentPoints().length - 1

                        x: handlePoint.x - width / 2
                        y: handlePoint.y - height / 2
                        selected: index === interaction.selectedIndex
                        endpoint: endpointHandle
                        accent: root.accent
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
                    property point pressPosition: Qt.point(0, 0)
                    property point handleOrigin: Qt.point(0, 0)
                    property var startingPoints: []
                    property bool gestureActive: false
                    property bool fineMode: false
                    property int axisLock: 0

                    function finishGesture(cancel) {
                        if (!gestureActive)
                            return;
                        gestureActive = false;
                        draggingIndex = -1;
                        if (cancel)
                            root.pointsEdited(startingPoints);
                        root.editFinished();
                    }

                    onPressed: function (mouse) {
                        root.forceActiveFocus();
                        startingPoints = root.currentPoints();
                        gestureActive = true;
                        fineMode = (mouse.modifiers & Qt.ShiftModifier) !== 0;
                        axisLock = 0;
                        root.editStarted();
                        root.clearPresetPreview();
                        const handleIndex = root.closestHandle(mouse.x, mouse.y);
                        if (handleIndex >= 0) {
                            draggingIndex = handleIndex;
                            selectedIndex = handleIndex;
                        } else {
                            const insertedIndex = root.insertPoint(mouse.x, mouse.y);
                            draggingIndex = insertedIndex;
                            selectedIndex = insertedIndex;
                        }
                        pressPosition = Qt.point(mouse.x, mouse.y);
                        if (draggingIndex >= 0)
                            handleOrigin = root.graphPoint(draggingIndex);
                        curveCanvas.requestPaint();
                    }

                    onPositionChanged: function (mouse) {
                        if (!gestureActive || draggingIndex < 0 || draggingIndex >= root.currentPoints().length)
                            return;
                        const dx = mouse.x - pressPosition.x;
                        const dy = mouse.y - pressPosition.y;
                        pressPosition = Qt.point(mouse.x, mouse.y);
                        const fine = (mouse.modifiers & Qt.ShiftModifier) !== 0;
                        if (fine !== fineMode) {
                            fineMode = fine;
                            return;
                        }
                        if ((mouse.modifiers & Qt.ControlModifier) !== 0) {
                            if (axisLock === 0 && (dx !== 0 || dy !== 0))
                                axisLock = Math.abs(dx) >= Math.abs(dy) ? 1 : 2;
                        } else {
                            axisLock = 0;
                        }
                        const scale = fineMode ? 0.1 : 1;
                        root.updatePoint(draggingIndex,
                                         handleOrigin.x + (axisLock === 2 ? 0 : dx * scale),
                                         handleOrigin.y + (axisLock === 1 ? 0 : dy * scale));
                        handleOrigin = root.graphPoint(draggingIndex);
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

                    onReleased: finishGesture(false)
                    onCanceled: finishGesture(true)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: "Point " + (root.effectiveSelectedIndex() + 1)
                color: theme.textFaint
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                font.weight: Font.DemiBold
            }

            Label {
                text: "In"
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
            }

            ToneTextField {
                objectName: "toneCurveInputField"
                Layout.preferredWidth: theme.valueColumnWidth
                text: root.selectedInputText()
                enabled: root.effectiveSelectedIndex() > 0 && root.effectiveSelectedIndex() < root.currentPoints().length - 1
                horizontalAlignment: TextInput.AlignHCenter
                validator: DoubleValidator { bottom: 0; top: 255; decimals: 3; notation: DoubleValidator.StandardNotation }
                onAccepted: root.updateSelectedNumeric("x", text)
                onEditingFinished: root.updateSelectedNumeric("x", text)
            }

            Label {
                text: "Out"
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
            }

            ToneTextField {
                objectName: "toneCurveOutputField"
                Layout.preferredWidth: theme.valueColumnWidth
                text: root.selectedOutputText()
                horizontalAlignment: TextInput.AlignHCenter
                validator: DoubleValidator { bottom: 0; top: 255; decimals: 3; notation: DoubleValidator.StandardNotation }
                onAccepted: root.updateSelectedNumeric("y", text)
                onEditingFinished: root.updateSelectedNumeric("y", text)
            }

            Item {
                Layout.fillWidth: true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: "Shift: fine · Ctrl: lock axis · Arrows: nudge · Esc: cancel"
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WordWrap
            }
            MiniButton {
                objectName: "toneCurveExpandButton"
                visible: root.compactMode
                text: root.expanded ? "Collapse" : "Expand"
                onClicked: root.expanded = !root.expanded
            }
        }
    }

    onPointsModelChanged: curveCanvas.requestPaint()
    onSelectedChannelChanged: curveCanvas.requestPaint()
    onHistogramRedDataChanged: curveCanvas.requestPaint()
    onHistogramGreenDataChanged: curveCanvas.requestPaint()
    onHistogramBlueDataChanged: curveCanvas.requestPaint()
    onHistogramLuminanceDataChanged: curveCanvas.requestPaint()
    onHistogramLogScaleChanged: curveCanvas.requestPaint()
}
