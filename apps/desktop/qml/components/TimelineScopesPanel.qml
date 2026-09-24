pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    property var scopeData: ({})
    property var audioPhaseData: ({})
    property bool busy: false
    property bool frozen: false
    property bool compact: false
    property string activeScope: "waveform"
    property string placement: compact ? "docked" : "window"
    readonly property string timelineScopesPanelContract: "timeline-scopes-panel:v1|video:waveform-vectorscope-rgb-parade-histogram|audio:decoded-post-processing-phase-points-correlation|source:captured-actual-program|analysis:bounded-480x270-worker-latest-wins|updates:four-fps-live-freeze-manual|layout:one-calm-docked-all-five-popout|chrome:shared-dark-borderless"
    readonly property var scopeKinds: [
        {"key": "waveform", "label": "Waveform", "icon": "scope"},
        {"key": "vectorscope", "label": "Vectorscope", "icon": "scope"},
        {"key": "parade", "label": "RGB Parade", "icon": "scope"},
        {"key": "histogram", "label": "Histogram", "icon": "scope"},
        {"key": "phase", "label": "Audio Phase", "icon": "audio"}
    ]

    signal activeScopeChangeRequested(string scope)
    signal freezeToggleRequested()
    signal refreshRequested()
    signal detachToggleRequested()
    signal closeRequested()

    color: theme.ink900
    radius: compact ? theme.radius4 : 0
    border.width: 0
    clip: true
    Accessible.role: Accessible.Pane
    Accessible.name: "Program scopes"
    Accessible.description: "Waveform, vectorscope, RGB parade, histogram, and decoded stereo phase analysis"

    function scopeLabel(key) {
        for (let index = 0; index < scopeKinds.length; ++index) {
            if (scopeKinds[index].key === key)
                return scopeKinds[index].label
        }
        return "Waveform"
    }

    component ScopeTile: Rectangle {
        id: tile
        required property string kind
        property bool single: false
        readonly property string readableLabel: root.scopeLabel(kind)
        objectName: "timelineScope" + readableLabel.replace(/ /g, "") + "Tile"
        color: root.theme.alpha(root.theme.ink950, 0.62)
        radius: root.theme.radius4
        border.width: 0
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: single ? root.theme.scaled(132) : root.theme.scaled(190)
        Accessible.role: Accessible.Chart
        Accessible.name: readableLabel

        Label {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: root.theme.space8
            anchors.topMargin: root.theme.space6
            text: tile.readableLabel.toUpperCase()
            color: root.theme.textMuted
            font.family: root.theme.fontMono
            font.pixelSize: root.theme.typeMicro
            font.weight: Font.DemiBold
            z: 2
        }

        Label {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: root.theme.space8
            anchors.topMargin: root.theme.space6
            visible: tile.kind === "phase"
            text: {
                const correlation = Number(root.audioPhaseData.correlation || 0)
                return correlation > 0.25 ? "IN PHASE  +" + correlation.toFixed(2)
                     : correlation < -0.25 ? "OUT OF PHASE  " + correlation.toFixed(2)
                     : "WIDE  " + correlation.toFixed(2)
            }
            color: Number(root.audioPhaseData.correlation || 0) < -0.25
                   ? root.theme.error : root.theme.textMuted
            font.family: root.theme.fontMono
            font.pixelSize: root.theme.typeMicro
            z: 2
        }

        Canvas {
            id: scopeCanvas
            objectName: tile.objectName + "Canvas"
            anchors.fill: parent
            anchors.leftMargin: root.theme.space8
            anchors.rightMargin: root.theme.space8
            anchors.topMargin: root.theme.scaled(24)
            anchors.bottomMargin: root.theme.space8
            renderTarget: Canvas.Image
            antialiasing: true

            function bounded(value) {
                return Math.max(0, Math.min(1, Number(value || 0)))
            }

            function drawGrid(context, left, top, plotWidth, plotHeight, labels) {
                context.save()
                context.strokeStyle = root.theme.alpha(root.theme.polar, 0.08)
                context.lineWidth = 1
                for (let index = 0; index <= 4; ++index) {
                    const x = Math.round(left + plotWidth * index / 4) + 0.5
                    const y = Math.round(top + plotHeight * index / 4) + 0.5
                    context.beginPath()
                    context.moveTo(x, top)
                    context.lineTo(x, top + plotHeight)
                    context.moveTo(left, y)
                    context.lineTo(left + plotWidth, y)
                    context.stroke()
                }
                if (labels) {
                    context.fillStyle = root.theme.alpha(root.theme.textFaint, 0.72)
                    context.font = "8px \"" + root.theme.fontMono + "\""
                    context.textAlign = "left"
                    context.textBaseline = "middle"
                    const levels = [["100", 0], ["75", 0.25], ["50", 0.5], ["25", 0.75], ["0", 1]]
                    for (let labelIndex = 0; labelIndex < levels.length; ++labelIndex)
                        context.fillText(levels[labelIndex][0], 1,
                                         top + levels[labelIndex][1] * plotHeight)
                }
                context.restore()
            }

            function drawDensity(context, data, color, left, top, plotWidth, plotHeight, alphaScale) {
                const columns = Math.max(1, Number(root.scopeData.waveformColumns || 96))
                const levels = Math.max(1, Number(root.scopeData.waveformLevels || 64))
                if (!data || data.length < columns * levels)
                    return
                const cellWidth = plotWidth / columns
                const cellHeight = plotHeight / levels
                context.save()
                context.globalCompositeOperation = "lighter"
                for (let column = 0; column < columns; ++column) {
                    for (let level = 0; level < levels; ++level) {
                        const density = bounded(data[column * levels + level])
                        if (density < 0.018)
                            continue
                        context.fillStyle = root.theme.alpha(color,
                            Math.min(0.82, Math.pow(density, 0.72) * alphaScale))
                        context.fillRect(left + column * cellWidth,
                                         top + plotHeight - (level + 1) * cellHeight,
                                         Math.max(1, cellWidth + 0.2),
                                         Math.max(1, cellHeight + 0.2))
                    }
                }
                context.restore()
            }

            function drawTrace(context, data, color, left, top, plotWidth, plotHeight, alpha) {
                if (!data || data.length < 2)
                    return
                context.save()
                context.strokeStyle = root.theme.alpha(color, alpha)
                context.lineWidth = 1.15
                context.beginPath()
                for (let index = 0; index < data.length; ++index) {
                    const x = left + plotWidth * index / Math.max(1, data.length - 1)
                    const y = top + plotHeight - bounded(data[index]) * plotHeight
                    if (index === 0) context.moveTo(x, y); else context.lineTo(x, y)
                }
                context.stroke()
                context.restore()
            }

            function drawHistogram(context) {
                drawGrid(context, 0, 0, width, height, false)
                const traces = [
                    [root.scopeData.histogramLuma || [], root.theme.polar, 0.72],
                    [root.scopeData.histogramRed || [], root.theme.signalRed, 0.62],
                    [root.scopeData.histogramGreen || [], root.theme.vectorGreen, 0.58],
                    [root.scopeData.histogramBlue || [], root.theme.signalCyan, 0.62]
                ]
                for (let traceIndex = 0; traceIndex < traces.length; ++traceIndex) {
                    const values = traces[traceIndex][0]
                    if (!values || values.length < 2)
                        continue
                    context.save()
                    context.strokeStyle = root.theme.alpha(traces[traceIndex][1], traces[traceIndex][2])
                    context.lineWidth = traceIndex === 0 ? 1.25 : 1
                    context.beginPath()
                    for (let index = 0; index < values.length; ++index) {
                        const x = width * index / Math.max(1, values.length - 1)
                        const y = height - bounded(values[index]) * (height - 2) - 1
                        if (index === 0) context.moveTo(x, y); else context.lineTo(x, y)
                    }
                    context.stroke()
                    context.restore()
                }
            }

            function drawWaveform(context) {
                const labelWidth = 21
                drawGrid(context, labelWidth, 0, width - labelWidth, height, true)
                drawDensity(context, root.scopeData.densityLuma || [], root.theme.polar,
                            labelWidth, 0, width - labelWidth, height, 0.55)
                drawDensity(context, root.scopeData.densityBlue || [], root.theme.signalCyan,
                            labelWidth, 0, width - labelWidth, height, 0.16)
                drawDensity(context, root.scopeData.densityRed || [], root.theme.signalRed,
                            labelWidth, 0, width - labelWidth, height, 0.14)
                drawTrace(context, root.scopeData.waveformLuma || [], root.theme.polar,
                          labelWidth, 0, width - labelWidth, height, 0.62)
            }

            function drawParade(context) {
                const labelWidth = 21
                const gap = 7
                const segment = Math.max(1, (width - labelWidth - gap * 2) / 3)
                drawGrid(context, labelWidth, 0, width - labelWidth, height, true)
                const channels = [
                    [root.scopeData.densityRed || [], root.scopeData.waveformRed || [], root.theme.signalRed],
                    [root.scopeData.densityGreen || [], root.scopeData.waveformGreen || [], root.theme.vectorGreen],
                    [root.scopeData.densityBlue || [], root.scopeData.waveformBlue || [], root.theme.signalCyan]
                ]
                for (let channel = 0; channel < channels.length; ++channel) {
                    const left = labelWidth + channel * (segment + gap)
                    drawDensity(context, channels[channel][0], channels[channel][2],
                                left, 0, segment, height, 0.72)
                    drawTrace(context, channels[channel][1], channels[channel][2],
                              left, 0, segment, height, 0.72)
                }
            }

            function drawVectorscope(context) {
                const centerX = width / 2
                const centerY = height / 2
                const radius = Math.max(1, Math.min(width, height) * 0.43)
                context.save()
                context.strokeStyle = root.theme.alpha(root.theme.polar, 0.14)
                context.lineWidth = 1
                context.beginPath()
                context.arc(centerX, centerY, radius, 0, Math.PI * 2)
                context.moveTo(centerX - radius, centerY)
                context.lineTo(centerX + radius, centerY)
                context.moveTo(centerX, centerY - radius)
                context.lineTo(centerX, centerY + radius)
                context.stroke()
                const targets = [
                    ["R", -0.36, -0.28], ["Mg", 0.30, -0.32], ["B", 0.42, 0.16],
                    ["Cy", 0.30, 0.34], ["G", -0.36, 0.30], ["Yl", -0.43, -0.02]
                ]
                context.font = "8px \"" + root.theme.fontMono + "\""
                context.textAlign = "center"
                context.textBaseline = "middle"
                context.fillStyle = root.theme.alpha(root.theme.textFaint, 0.68)
                for (let target = 0; target < targets.length; ++target)
                    context.fillText(targets[target][0], centerX + targets[target][1] * radius * 1.8,
                                     centerY + targets[target][2] * radius * 1.8)
                context.restore()
                const points = root.scopeData.vectorscopePoints || []
                context.save()
                context.globalCompositeOperation = "lighter"
                for (let index = 0; index < points.length; ++index) {
                    const point = points[index] || ({})
                    context.fillStyle = Qt.rgba(Number(point.red || 0.7),
                                                Number(point.green || 0.7),
                                                Number(point.blue || 0.7), 0.12)
                    context.fillRect(centerX + (bounded(point.x) - 0.5) * radius * 2 - 0.7,
                                     centerY + (bounded(point.y) - 0.5) * radius * 2 - 0.7,
                                     1.5, 1.5)
                }
                context.restore()
            }

            function drawPhase(context) {
                const plotHeight = Math.max(1, height - 18)
                const centerX = width / 2
                const centerY = plotHeight / 2
                context.save()
                context.strokeStyle = root.theme.alpha(root.theme.polar, 0.10)
                context.lineWidth = 1
                context.beginPath()
                context.moveTo(0, centerY)
                context.lineTo(width, centerY)
                context.moveTo(centerX, 0)
                context.lineTo(centerX, plotHeight)
                context.moveTo(centerX - plotHeight / 2, plotHeight)
                context.lineTo(centerX + plotHeight / 2, 0)
                context.moveTo(centerX - plotHeight / 2, 0)
                context.lineTo(centerX + plotHeight / 2, plotHeight)
                context.stroke()
                const points = root.audioPhaseData.points || []
                context.globalCompositeOperation = "lighter"
                for (let index = 0; index < points.length; ++index) {
                    const left = Math.max(-1, Math.min(1, Number(points[index].left || 0)))
                    const right = Math.max(-1, Math.min(1, Number(points[index].right || 0)))
                    const side = (left - right) * 0.5
                    const mid = (left + right) * 0.5
                    context.fillStyle = root.theme.alpha(root.theme.accentCool300, 0.24)
                    context.fillRect(centerX + side * width * 0.46 - 0.8,
                                     centerY - mid * plotHeight * 0.46 - 0.8, 1.7, 1.7)
                }
                context.restore()
                const correlation = Math.max(-1, Math.min(1, Number(root.audioPhaseData.correlation || 0)))
                const barY = height - 8
                context.fillStyle = root.theme.alpha(root.theme.polar, 0.10)
                context.fillRect(0, barY, width, 4)
                context.fillStyle = correlation < -0.25 ? root.theme.error
                                  : correlation < 0.25 ? root.theme.warning
                                  : root.theme.success
                const zeroX = width / 2
                context.fillRect(Math.min(zeroX, zeroX + correlation * zeroX), barY,
                                 Math.max(2, Math.abs(correlation) * zeroX), 4)
            }

            onPaint: {
                const context = getContext("2d")
                context.reset()
                if (tile.kind === "histogram") drawHistogram(context)
                else if (tile.kind === "waveform") drawWaveform(context)
                else if (tile.kind === "parade") drawParade(context)
                else if (tile.kind === "vectorscope") drawVectorscope(context)
                else drawPhase(context)
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onVisibleChanged: if (visible) requestPaint()

            Connections {
                target: root
                function onScopeDataChanged() { scopeCanvas.requestPaint() }
                function onAudioPhaseDataChanged() { scopeCanvas.requestPaint() }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.compact ? root.theme.space8 : root.theme.space12
        spacing: root.theme.space8

        RowLayout {
            Layout.fillWidth: true
            spacing: root.theme.space6

            Label {
                text: root.compact ? root.scopeLabel(root.activeScope) : "Program Scopes"
                color: root.theme.textStrong
                font.family: root.theme.fontUi
                font.pixelSize: root.theme.typeSection
                font.weight: Font.DemiBold
            }

            StatusChip {
                objectName: "timelineScopesStatusChip"
                text: root.frozen ? "FROZEN" : root.busy ? "ANALYZING" : "LIVE · 4 FPS"
                tone: root.frozen ? "warning" : root.busy ? "info" : "success"
                busy: root.busy && !root.frozen
                compact: true
                tooltipText: root.frozen
                             ? "Scopes are holding the last completed frame."
                             : "Video scopes analyze a bounded Program capture on a worker thread."
            }

            Label {
                Layout.fillWidth: true
                visible: !root.compact
                text: Number(root.scopeData.sampleCount || 0) > 0
                      ? Number(root.scopeData.width || 0) + " × " + Number(root.scopeData.height || 0)
                        + " sample · " + Number(root.scopeData.elapsedMs || 0) + " ms worker"
                      : "Waiting for Program frame"
                color: root.theme.textFaint
                font.family: root.theme.fontMono
                font.pixelSize: root.theme.typeMicro
            }

            Item { Layout.fillWidth: root.compact }

            ActionButton {
                objectName: "timelineScopesFreezeButton"
                text: root.frozen ? "Resume" : "Freeze"
                iconName: root.frozen ? "play" : "pause"
                compact: true
                active: root.frozen
                tooltipText: root.frozen ? "Resume live scope updates." : "Hold the current analysis frame."
                onClicked: root.freezeToggleRequested()
            }

            ActionButton {
                objectName: "timelineScopesRefreshButton"
                text: "Refresh"
                iconName: "reset"
                compact: true
                tooltipText: "Analyze the current Program frame now."
                onClicked: root.refreshRequested()
            }

            ActionButton {
                objectName: "timelineScopesDetachButton"
                text: root.compact ? "Pop Out" : "Dock"
                iconName: root.compact ? "fullscreen" : "panel-bottom"
                compact: true
                tooltipText: root.compact ? "Open all five scopes in a tool window." : "Return scopes below Program."
                onClicked: root.detachToggleRequested()
            }

            ActionButton {
                objectName: "timelineScopesCloseButton"
                text: "Close"
                iconName: "close"
                compact: true
                tooltipText: "Close Program scopes."
                onClicked: root.closeRequested()
            }
        }

        RowLayout {
            objectName: "timelineScopesDockSelector"
            visible: root.compact
            Layout.fillWidth: true
            spacing: root.theme.space4

            Repeater {
                model: root.scopeKinds
                delegate: MiniButton {
                    required property var modelData
                    Layout.fillWidth: true
                    text: String(modelData.label)
                    role: "segmented"
                    active: root.activeScope === String(modelData.key)
                    tooltip: "Show " + String(modelData.label) + " in the dock."
                    onClicked: root.activeScopeChangeRequested(String(modelData.key))
                }
            }
        }

        ScopeTile {
            visible: root.compact
            Layout.fillWidth: true
            Layout.fillHeight: true
            kind: root.activeScope
            single: true
        }

        GridLayout {
            objectName: "timelineScopesAllGrid"
            visible: !root.compact
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: width >= root.theme.scaled(980) ? 3 : 2
            columnSpacing: root.theme.space8
            rowSpacing: root.theme.space8

            Repeater {
                model: root.scopeKinds
                delegate: ScopeTile {
                    required property var modelData
                    kind: String(modelData.key)
                    single: false
                }
            }
        }
    }
}
