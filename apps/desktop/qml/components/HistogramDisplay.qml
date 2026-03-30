import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    implicitHeight: 100

    property var redData: []
    property var greenData: []
    property var blueData: []
    property var luminanceData: []
    property bool showLuminance: true
    property bool showRgb: true

    Theme { id: theme }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.3)
        radius: 4

        Canvas {
            id: histogramCanvas
            anchors.fill: parent
            anchors.margins: 2

            onPaint: {
                var ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);

                var w = width;
                var h = height;

                if (root.showLuminance && root.luminanceData.length === 256) {
                    drawChannel(ctx, root.luminanceData, w, h,
                                Qt.rgba(0.75, 0.75, 0.75, 0.35));
                }
                if (root.showRgb) {
                    if (root.blueData.length === 256) {
                        drawChannel(ctx, root.blueData, w, h,
                                    Qt.rgba(0.25, 0.45, 1.0, 0.45));
                    }
                    if (root.greenData.length === 256) {
                        drawChannel(ctx, root.greenData, w, h,
                                    Qt.rgba(0.2, 0.85, 0.3, 0.45));
                    }
                    if (root.redData.length === 256) {
                        drawChannel(ctx, root.redData, w, h,
                                    Qt.rgba(1.0, 0.25, 0.25, 0.45));
                    }
                }
            }

            function drawChannel(ctx, data, w, h, color) {
                ctx.beginPath();
                ctx.moveTo(0, h);
                var step = w / 255;
                for (var i = 0; i < 256; i++) {
                    var val = data[i] || 0;
                    var x = i * step;
                    var y = h - val * h;
                    ctx.lineTo(x, y);
                }
                ctx.lineTo(w, h);
                ctx.closePath();
                ctx.fillStyle = color;
                ctx.fill();
            }
        }
    }

    Connections {
        target: root
        function onRedDataChanged() { histogramCanvas.requestPaint() }
        function onGreenDataChanged() { histogramCanvas.requestPaint() }
        function onBlueDataChanged() { histogramCanvas.requestPaint() }
        function onLuminanceDataChanged() { histogramCanvas.requestPaint() }
    }
}
