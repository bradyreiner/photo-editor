import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    property real diameter: 32
    property real hardness: 0.8
    property real flow: 1
    property real strokeOpacity: 1
    property real spacing: 0.15
    property color ink: "white"
    property bool erase: false
    property var samples: []
    readonly property bool hasPracticeStroke: samples.length > 0
    Theme { id: theme }
    implicitHeight: theme.scaled(104)
    radius: theme.radiusControl
    color: theme.alpha(theme.ink950, 0.5)
    border.width: theme.hairlineWidth
    border.color: theme.lineSoft
    clip: true

    function resetSample() { samples = []; preview.requestPaint() }
    function appendSample(x, y, first) {
        const point = {x: Math.max(0, Math.min(1, x / Math.max(1, preview.width))),
                       y: Math.max(0, Math.min(1, y / Math.max(1, preview.height)))}
        if (first) samples = [point]
        else if (samples.length < 256) samples = samples.concat([point])
        preview.requestPaint()
    }
    RowLayout {
        id: titleRow
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: theme.space8
        Label {
            Layout.fillWidth: true
            text: root.hasPracticeStroke ? qsTr("Practice stroke") : qsTr("Tip preview")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeMicro
        }
        MiniButton {
            objectName: "brushPreviewReset"
            text: qsTr("Reset")
            role: "quiet"
            enabled: root.hasPracticeStroke
            tooltip: qsTr("Clear the practice stroke. Your photo is unchanged.")
            onClicked: root.resetSample()
        }
    }
    Canvas {
        id: preview
        objectName: "brushPracticeCanvas"
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: titleRow.bottom; anchors.bottom: hint.top
        anchors.leftMargin: theme.space8; anchors.rightMargin: theme.space8
        opacity: root.strokeOpacity
        Accessible.name: qsTr("Brush tip practice area")
        Accessible.description: qsTr("Draw here to preview round tip size, edge, flow and opacity. Scaled illustration; material texture and document pixels may differ. Your photo is unchanged.")
        property real tipSize: root.diameter
        property real edge: root.hardness
        property real strength: root.flow
        property real dabSpacing: root.spacing
        property color tipColor: root.erase ? theme.textStrong : root.ink
        onTipSizeChanged: requestPaint()
        onEdgeChanged: requestPaint()
        onStrengthChanged: requestPaint()
        onDabSpacingChanged: requestPaint()
        onTipColorChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            if (width <= 0 || height <= 0) return
            const radius = Math.max(1.5, Math.min(height * 0.38, Math.sqrt(tipSize) * 1.3))
            const points = root.hasPracticeStroke ? root.samples : []
            if (!root.hasPracticeStroke) {
                for (let i = 0; i <= 40; ++i) {
                    const t = i / 40
                    points.push({x: 0.1 + 0.8 * t, y: 0.5 + Math.sin(t * Math.PI * 2) * 0.17})
                }
            }
            let dabs = 0
            const dab = function(x, y) {
                if (++dabs > 2048) return
                const gradient = ctx.createRadialGradient(x, y, 0, x, y, radius)
                const c = preview.tipColor
                const solid = Qt.rgba(c.r, c.g, c.b, c.a)
                gradient.addColorStop(0, solid)
                gradient.addColorStop(Math.min(0.999, Math.max(0, preview.edge)), solid)
                gradient.addColorStop(1, Qt.rgba(c.r, c.g, c.b, 0))
                ctx.fillStyle = gradient
                ctx.globalAlpha = Math.max(0, Math.min(1, preview.strength))
                ctx.beginPath(); ctx.arc(x, y, radius, 0, Math.PI * 2); ctx.fill()
            }
            let carry = 0
            const step = Math.max(0.75, radius * 2 * dabSpacing)
            if (points.length) dab(points[0].x * width, points[0].y * height)
            for (let i = 1; i < points.length && dabs < 2048; ++i) {
                const a = points[i - 1], b = points[i]
                const dx = (b.x - a.x) * width, dy = (b.y - a.y) * height
                const distance = Math.hypot(dx, dy)
                let travel = step - carry
                for (; travel <= distance && distance > 0 && dabs < 2048; travel += step)
                    dab(a.x * width + dx * travel / distance, a.y * height + dy * travel / distance)
                carry = distance - (travel - step)
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.CrossCursor
            preventStealing: pressed
            onPressed: function(mouse) { root.appendSample(mouse.x, mouse.y, true) }
            onPositionChanged: function(mouse) { if (pressed) root.appendSample(mouse.x, mouse.y, false) }
        }
    }
    Label {
        id: hint
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: theme.space8
        text: qsTr("Draw to try · Scaled tip preview")
        color: theme.textFaint
        font.family: theme.fontUi
        font.pixelSize: theme.typeMicro
        wrapMode: Text.Wrap
    }
}
