import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: scopeTile

    required property Item workspace
    required property var previewController
    property string kind: "Waveform"
    property bool large: false
    readonly property real preferredTileHeight: Layout.preferredHeight

    Theme { id: theme }

    Layout.fillWidth: true
    Layout.fillHeight: large
    Layout.minimumHeight: large ? 220 : 112
    Layout.preferredHeight: large ? (workspace.scopeLayoutPreset === "Quad" ? 320 : 260) : 142
    radius: 0
    color: "transparent"
    border.width: 0
    border.color: theme.lineSoft

    Label {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 10
        anchors.topMargin: 8
        text: scopeTile.kind
        color: theme.textMain
        font.family: theme.fontUi
        font.pixelSize: theme.typeLabel
        font.weight: Font.Medium
        z: 2
    }

    HistogramDisplay {
        anchors.fill: parent
        anchors.margins: 10
        anchors.topMargin: 26
        visible: scopeTile.kind === "Histogram"
        redData: previewController.histogramRed
        greenData: previewController.histogramGreen
        blueData: previewController.histogramBlue
        luminanceData: previewController.histogramLuminance
        showRgb: workspace.scopeShowRgb
        showLuminance: workspace.scopeShowLuma
        logScale: workspace.scopeLogHistogram
        framed: false
        shadowClipRatio: previewController.histogramSampleCount > 0
                         ? previewController.histogramShadowClipCount / previewController.histogramSampleCount
                         : 0
        highlightClipRatio: previewController.histogramSampleCount > 0
                            ? previewController.histogramHighlightClipCount / previewController.histogramSampleCount
                            : 0
    }

    Canvas {
        id: scopeCanvas
        objectName: "studioScopeCanvas"
        anchors.fill: parent
        anchors.margins: 10
        anchors.topMargin: 26
        visible: scopeTile.kind !== "Histogram"

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onVisibleChanged: requestPaint()

        onPaint: {
            // Histogram uses its own canvas. Hidden scopes can still receive
            // histogram notifications; repaint when visible instead.
            if (!visible || scopeTile.kind === "Histogram")
                return
            const ctx = getContext("2d")
            ctx.reset()
            const w = width
            const h = height
            const plotY = 7
            const plotHeight = Math.max(1, h - plotY * 2)
            const red = previewController.scopeWaveformRed || []
            const green = previewController.scopeWaveformGreen || []
            const blue = previewController.scopeWaveformBlue || []
            const luma = previewController.scopeWaveformLuminance || []
            const densityRed = previewController.scopeDensityRed || []
            const densityGreen = previewController.scopeDensityGreen || []
            const densityBlue = previewController.scopeDensityBlue || []
            const densityLuma = previewController.scopeDensityLuminance || []
            const vectors = previewController.scopeVectorscopePoints || []
            const densityLevels = 64

            function drawGrid(x, y, plotWidth, plotHeight, labels) {
                if (!workspace.scopeShowGrid)
                    return
                ctx.save()
                ctx.lineWidth = 1
                ctx.strokeStyle = theme.alpha(theme.polar, 0.075)
                for (let i = 1; i < 4; ++i) {
                    const gx = Math.round(x + (plotWidth / 4) * i) + 0.5
                    ctx.beginPath()
                    ctx.moveTo(gx, y)
                    ctx.lineTo(gx, y + plotHeight)
                    ctx.stroke()
                }
                for (let j = 0; j <= 4; ++j) {
                    const gy = Math.round(y + (plotHeight / 4) * j) + 0.5
                    ctx.beginPath()
                    ctx.moveTo(x, gy)
                    ctx.lineTo(x + plotWidth, gy)
                    ctx.stroke()
                }
                ctx.strokeStyle = theme.alpha(theme.lineSoft, 0.20)
                ctx.strokeRect(Math.round(x) + 0.5, Math.round(y) + 0.5, Math.max(0, plotWidth - 1), Math.max(0, plotHeight - 1))
                if (labels) {
                    ctx.fillStyle = theme.textMuted
                    ctx.font = "10px \"" + theme.fontUi + "\""
                    ctx.textAlign = "left"
                    ctx.textBaseline = "middle"
                    const values = [["100", 0], ["75", 0.25], ["50", 0.5], ["25", 0.75], ["0", 1]]
                    for (let k = 0; k < values.length; ++k)
                        ctx.fillText(values[k][0], 0, y + values[k][1] * plotHeight)
                }
                ctx.restore()
            }

            function channelValue(data, index, key) {
                if (!data || data.length === 0)
                    return 0
                const raw = data[Math.max(0, Math.min(data.length - 1, index))] || 0
                const value = typeof raw === "object" ? Number(raw[key] || 0) : Number(raw || 0)
                return Math.max(0, Math.min(1, value))
            }

            function drawAverage(data, color, x, y, plotWidth, plotHeight, alpha, lineWidth) {
                if (!workspace.scopeShowTrace)
                    return
                const count = data ? data.length : 0
                if (count < 2)
                    return
                const step = plotWidth / Math.max(1, count - 1)
                ctx.save()
                ctx.strokeStyle = theme.alpha(color, alpha)
                ctx.lineWidth = lineWidth
                ctx.beginPath()
                for (let i = 0; i < count; ++i) {
                    const px = x + step * i
                    const py = y + plotHeight - channelValue(data, i, "avg") * plotHeight
                    if (i === 0)
                        ctx.moveTo(px, py)
                    else
                        ctx.lineTo(px, py)
                }
                ctx.stroke()
                ctx.restore()
            }

            function drawDensity(data, color, x, y, plotWidth, plotHeight, alphaScale, pointScale) {
                if (!workspace.scopeShowDensity || !data || data.length < densityLevels)
                    return
                const columns = Math.max(1, Math.floor(data.length / densityLevels))
                const xStep = plotWidth / columns
                const yStep = plotHeight / densityLevels
                ctx.save()
                ctx.globalCompositeOperation = "lighter"
                for (let column = 0; column < columns; ++column) {
                    const px = x + column * xStep
                    for (let level = 0; level < densityLevels; ++level) {
                        const density = Number(data[column * densityLevels + level] || 0)
                        if (density <= 0.008)
                            continue
                        const alpha = Math.min(0.72, Math.pow(density, 1.35) * alphaScale)
                        if (alpha <= 0.006)
                            continue
                        ctx.fillStyle = theme.alpha(color, alpha)
                        ctx.fillRect(px, y + plotHeight - (level + 1) * yStep, Math.max(1, xStep * pointScale), Math.max(1, yStep * 1.05))
                    }
                }
                ctx.restore()
            }

            if (scopeTile.kind === "Parade") {
                const labelWidth = 20
                const gap = 8
                const plotX = labelWidth
                const segment = (w - labelWidth - gap * 2) / 3
                drawGrid(plotX, plotY, w - plotX, plotHeight, true)
                if (workspace.scopeShowRgb) {
                    drawDensity(densityRed, theme.signalRed, plotX, plotY, segment, plotHeight, 0.70, 1.08)
                    drawDensity(densityGreen, theme.vectorGreen, plotX + segment + gap, plotY, segment, plotHeight, 0.62, 1.08)
                    drawDensity(densityBlue, theme.signalCyan, plotX + segment * 2 + gap * 2, plotY, segment, plotHeight, 0.62, 1.08)
                    drawAverage(red, theme.signalRed, plotX, plotY, segment, plotHeight, 0.62, 1.1)
                    drawAverage(green, theme.vectorGreen, plotX + segment + gap, plotY, segment, plotHeight, 0.58, 1.1)
                    drawAverage(blue, theme.signalCyan, plotX + segment * 2 + gap * 2, plotY, segment, plotHeight, 0.58, 1.1)
                }
                return
            }

            if (scopeTile.kind === "Vectorscope") {
                const cx = w / 2
                const cy = h / 2
                const radius = Math.max(0, Math.min(w, h) * 0.40)
                if (radius <= 0)
                    return
                ctx.save()
                ctx.strokeStyle = theme.alpha(theme.polar, 0.18)
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.arc(cx, cy, radius, 0, Math.PI * 2)
                ctx.stroke()
                ctx.strokeStyle = theme.alpha(theme.polar, 0.075)
                ctx.beginPath()
                ctx.arc(cx, cy, radius * 0.55, 0, Math.PI * 2)
                ctx.stroke()
                ctx.beginPath()
                ctx.moveTo(cx - radius, cy)
                ctx.lineTo(cx + radius, cy)
                ctx.moveTo(cx, cy - radius)
                ctx.lineTo(cx, cy + radius)
                ctx.stroke()
                ctx.restore()
                ctx.save()
                ctx.globalCompositeOperation = "lighter"
                for (let i = 0; i < vectors.length; ++i) {
                    const point = vectors[i]
                    const px = cx + (Number(point.x || 0.5) - 0.5) * radius * 1.75
                    const py = cy + (Number(point.y || 0.5) - 0.5) * radius * 1.75
                    ctx.fillStyle = Qt.rgba(Number(point.red || 0.8), Number(point.green || 0.8), Number(point.blue || 0.8), 0.045 + Number(point.luma || 0.5) * 0.07)
                    ctx.fillRect(px - 0.65, py - 0.65, 1.5, 1.5)
                }
                ctx.fillStyle = theme.alpha(theme.polar, 0.36)
                ctx.fillRect(cx - 1.1, cy - 1.1, 2.2, 2.2)
                ctx.restore()
                return
            }

            const plotX = 28
            drawGrid(plotX, plotY, w - plotX, plotHeight, true)
            if (workspace.scopeShowLuma) {
                drawDensity(densityLuma, theme.polar, plotX, plotY, w - plotX, plotHeight, 0.46, 1.06)
                drawAverage(luma, theme.polar, plotX, plotY, w - plotX, plotHeight, 0.58, 1.1)
            }
            if (workspace.scopeShowRgb) {
                drawDensity(densityBlue, theme.signalCyan, plotX, plotY, w - plotX, plotHeight, 0.16, 1.02)
                drawDensity(densityRed, theme.signalAmber, plotX, plotY, w - plotX, plotHeight, 0.13, 1.02)
                drawAverage(blue, theme.signalCyan, plotX, plotY, w - plotX, plotHeight, 0.24, 0.9)
                drawAverage(red, theme.signalAmber, plotX, plotY, w - plotX, plotHeight, 0.20, 0.9)
            }
        }

        Component.onCompleted: requestPaint()

        Connections {
            target: scopeTile.previewController
            function onHistogramChanged() { scopeCanvas.requestPaint() }
        }
        Connections {
            target: scopeTile.workspace
            function onScopeShowRgbChanged() { scopeCanvas.requestPaint() }
            function onScopeShowLumaChanged() { scopeCanvas.requestPaint() }
            function onScopeShowDensityChanged() { scopeCanvas.requestPaint() }
            function onScopeShowTraceChanged() { scopeCanvas.requestPaint() }
            function onScopeShowGridChanged() { scopeCanvas.requestPaint() }
        }
    }
}
