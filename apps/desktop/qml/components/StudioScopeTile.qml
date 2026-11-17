import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PhotoEditor

Rectangle {
    id: scopeTile
    required property Item workspace
    required property var previewController
    property string kind: "Waveform"
    property bool large: false
    readonly property real preferredTileHeight: Layout.preferredHeight
    readonly property var frame: previewController.scopeFrame || ({})
    readonly property real rangeMinimum: Number(frame.minimum || 0)
    readonly property real rangeMaximum: Number(frame.maximum || 1)
    Theme { id: theme }
    Layout.fillWidth: true
    Layout.fillHeight: large
    Layout.minimumHeight: theme.scaled(large ? 220 : 112)
    Layout.preferredHeight: theme.scaled(large ? (workspace.scopeLayoutPreset === "Quad" ? 320 : 260) : 160)
    radius: 0
    color: "transparent"
    border.width: 0
    clip: true

    Label {
        anchors.left: parent.left; anchors.top: parent.top
        anchors.margins: theme.space8
        text: scopeTile.kind
        color: theme.textMain
        font.family: theme.fontUi; font.pixelSize: theme.typeLabel
        font.weight: Font.Medium
    }
    Label {
        anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: theme.space8
        text: previewController.scopesFrozen ? "Frozen" : ""
        color: theme.textMuted
        font.family: theme.fontUi; font.pixelSize: theme.typeCaption
    }
    HistogramDisplay {
        anchors.fill: parent; anchors.margins: theme.space8; anchors.topMargin: theme.scaled(30)
        visible: scopeTile.kind === "Histogram"
        redData: scopeTile.frame.histogramRed || []
        greenData: scopeTile.frame.histogramGreen || []
        blueData: scopeTile.frame.histogramBlue || []
        luminanceData: scopeTile.frame.histogramLuminance || []
        showRgb: workspace.scopeShowRgb
        showLuminance: workspace.scopeShowLuma
        logScale: workspace.scopeLogHistogram
        framed: false
        shadowClipRatio: Number(scopeTile.frame.sampleCount || 0) > 0
            ? Number(scopeTile.frame.shadowClipCount || 0) / scopeTile.frame.sampleCount : 0
        highlightClipRatio: Number(scopeTile.frame.sampleCount || 0) > 0
            ? Number(scopeTile.frame.highlightClipCount || 0) / scopeTile.frame.sampleCount : 0
    }
    Item {
        id: plot
        objectName: "studioScopeCanvas"
        anchors.fill: parent; anchors.margins: theme.space8
        anchors.topMargin: theme.scaled(30); anchors.bottomMargin: theme.scaled(22)
        visible: scopeTile.kind !== "Histogram"
        Canvas {
            id: gridCanvas
            anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onVisibleChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                if (!visible || width < 8 || height < 8 || !scopeTile.workspace.scopeShowGrid) return
                ctx.strokeStyle = theme.alpha(theme.polar, 0.12)
                ctx.fillStyle = theme.textMuted
                ctx.lineWidth = 1
                ctx.font = theme.typeMicro + "px sans-serif"
                if (scopeTile.kind === "Vectorscope") {
                    const size = Math.min(width, height)
                    const cx = width / 2, cy = height / 2, radius = size / 2 - 2
                    for (const fraction of [0.25, 0.5, 0.75, 1]) {
                        ctx.beginPath(); ctx.arc(cx, cy, radius * fraction, 0, Math.PI * 2); ctx.stroke()
                    }
                    ctx.beginPath(); ctx.moveTo(cx - radius, cy); ctx.lineTo(cx + radius, cy)
                    ctx.moveTo(cx, cy - radius); ctx.lineTo(cx, cy + radius); ctx.stroke()
                    // Reference boxes use the same Rec.709 Y'CbCr mapping as computation.
                    const targets = [["R",1,0,0],["Y",1,1,0],["G",0,1,0],
                                     ["C",0,1,1],["B",0,0,1],["M",1,0,1]]
                    for (const target of targets) {
                        const r = target[1] * 0.75, g = target[2] * 0.75, b = target[3] * 0.75
                        const y = 0.2126*r + 0.7152*g + 0.0722*b
                        const x = cx + (b-y)*0.565*size, py = cy - (r-y)*0.713*size
                        ctx.strokeRect(x-3,py-3,6,6); ctx.fillText(target[0],x+5,py+3)
                    }
                    ctx.strokeStyle = theme.alpha(theme.polar, 0.3)
                    ctx.beginPath(); ctx.moveTo(cx, cy)
                    ctx.lineTo(cx - radius * 0.5, cy - radius * 0.866); ctx.stroke()
                    return
                }
                const left = theme.scaled(32)
                for (let i = 0; i <= 4; ++i) {
                    const y = height * i / 4
                    ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(width, y); ctx.stroke()
                    const value = scopeTile.rangeMaximum - (scopeTile.rangeMaximum-scopeTile.rangeMinimum)*i/4
                    ctx.fillText((value * 100).toFixed(0), 0, Math.max(9, Math.min(height-1, y+3)))
                }
            }
        }
        ScopePlotItem {
            id: nativePlot
            objectName: "studioNativeScopePlot"
            x: scopeTile.kind === "Vectorscope" ? 0 : theme.scaled(32)
            width: Math.max(1, plot.width-x); height: plot.height
            frame: scopeTile.frame
            reference: {
                const held = scopeTile.workspace.scopeReferenceFrame || ({})
                return held.domain === scopeTile.frame.domain && held.minimum === scopeTile.frame.minimum
                    && held.maximum === scopeTile.frame.maximum && String(held.region) === String(scopeTile.frame.region)
                    ? held : ({})
            }
            options: ({mode: scopeTile.kind, gain: scopeTile.workspace.scopeDensityGain,
                rgb: scopeTile.workspace.scopeShowRgb, luma: scopeTile.workspace.scopeShowLuma,
                density: scopeTile.workspace.scopeShowDensity, trace: scopeTile.workspace.scopeShowTrace,
                palette: [theme.signalRed, theme.vectorGreen, theme.signalCyan, theme.polar]})
        }
        HoverHandler { id: probe }
    }
    Label {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: theme.space8
        text: {
            if (scopeTile.kind === "Histogram") return ""
            if (scopeTile.kind === "Vectorscope") return "75% targets · skin-tone guide"
            if (!probe.hovered) return "Level % · " + scopeTile.previewController.scopeRangeMode
            const x = Math.max(0, Math.min(1, (probe.point.position.x-nativePlot.x)/Math.max(1,nativePlot.width)))
            const y = Math.max(0, Math.min(1, probe.point.position.y/Math.max(1,plot.height)))
            const value = scopeTile.rangeMaximum - y*(scopeTile.rangeMaximum-scopeTile.rangeMinimum)
            return "X " + (x*100).toFixed(1) + "% · Level " + (value*100).toFixed(1) + "%"
        }
        color: theme.textMuted
        font.family: theme.fontUi; font.pixelSize: theme.typeMicro
    }
    Connections {
        target: scopeTile.workspace
        function onScopeShowGridChanged() { gridCanvas.requestPaint() }
    }
    onRangeMinimumChanged: gridCanvas.requestPaint()
    onRangeMaximumChanged: gridCanvas.requestPaint()
    onKindChanged: gridCanvas.requestPaint()
}
