import QtQuick

Item {
    id: root
    implicitHeight: 100

    property var redData: []
    property var greenData: []
    property var blueData: []
    property var luminanceData: []
    property bool showLuminance: true
    property bool showRgb: true
    property bool logScale: true
    property real shadowClipRatio: 0
    property real highlightClipRatio: 0

    Theme { id: theme }

    GlassSurface {
        anchors.fill: parent
        radius: theme.radiusPanel
        surfaceOpacity: 0.38
        edgeOpacity: 0.14

        Canvas {
            id: histogramCanvas
            anchors.fill: parent
            anchors.margins: 2

            onPaint: {
                var ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);

                var w = width;
                var h = height;

                drawGuides(ctx, w, h);
                drawClippingBand(ctx, 0, shadowClipWidth(w), h, Qt.rgba(0.32, 0.52, 0.92, 0.18));
                drawClippingBand(ctx, w - highlightClipWidth(w), highlightClipWidth(w), h, Qt.rgba(0.96, 0.68, 0.28, 0.18));

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

            function guideY(h, ratio) {
                return Math.round(h - ratio * h) + 0.5;
            }

            function shadowClipWidth(w) {
                return Math.max(0, Math.min(w * 0.12, w * root.shadowClipRatio * 4.0));
            }

            function highlightClipWidth(w) {
                return Math.max(0, Math.min(w * 0.12, w * root.highlightClipRatio * 4.0));
            }

            function drawGuides(ctx, w, h) {
                ctx.save();
                ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.08);
                ctx.lineWidth = 1;
                [0.25, 0.5, 0.75].forEach(function(ratio) {
                    var y = guideY(h, ratio);
                    ctx.beginPath();
                    ctx.moveTo(0, y);
                    ctx.lineTo(w, y);
                    ctx.stroke();
                });
                ctx.restore();
            }

            function drawClippingBand(ctx, x, bandWidth, h, color) {
                if (bandWidth <= 0.5)
                    return;
                ctx.save();
                ctx.fillStyle = color;
                ctx.fillRect(x, 0, bandWidth, h);
                ctx.restore();
            }

            function plotValue(value) {
                var v = Math.max(0, value || 0);
                if (!root.logScale)
                    return Math.min(1, v);
                return Math.log(1 + v * 31) / Math.log(32);
            }

            function drawChannel(ctx, data, w, h, color) {
                ctx.beginPath();
                ctx.moveTo(0, h);
                var step = w / 255;
                for (var i = 0; i < 256; i++) {
                    var val = plotValue(data[i] || 0);
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
        function onLogScaleChanged() { histogramCanvas.requestPaint() }
        function onShadowClipRatioChanged() { histogramCanvas.requestPaint() }
        function onHighlightClipRatioChanged() { histogramCanvas.requestPaint() }
    }
}
