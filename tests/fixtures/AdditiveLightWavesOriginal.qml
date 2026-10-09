import QtQuick

// Frozen pre-visibility-guard waveform oracle. The visible canvas commands and
// dimensions match the original AdditiveLightWaves.qml implementation.
Item {
    id: root
    property bool active: true
    property bool reducedMotion: false
    property real phase: 0
    property int probePaintedCount: 0
    readonly property bool motionEnabled: active && visible && !reducedMotion && !theme.reducedMotion
    implicitWidth: 360
    implicitHeight: 80
    clip: true
    Accessible.ignored: true

    QtObject {
        id: theme
        readonly property bool reducedMotion: false
    }

    Canvas {
        id: lightTexture
        width: root.width * 2
        height: root.height
        x: -root.phase * root.width
        onPainted: root.probePaintedCount += 1
        onPaint: {
            const c = getContext("2d")
            c.reset()
            if (root.width <= 0 || height <= 0) return
            const colors = ["#ff344f", "#25ec83", "#398cff"]
            const spread = [-1, 0, 1]
            c.globalCompositeOperation = "lighter"
            for (let channel = 0; channel < 3; ++channel) {
                c.strokeStyle = colors[channel]
                c.shadowColor = colors[channel]
                c.shadowBlur = Math.min(16, height * 0.16)
                for (let halo = 0; halo < 2; ++halo) {
                    c.globalAlpha = halo === 0 ? 0.13 : 0.78
                    c.lineWidth = halo === 0 ? Math.max(3, height * 0.085) : Math.max(1, height * 0.017)
                    c.beginPath()
                    for (let step = 0; step <= 180; ++step) {
                        const t = step / 180 * 2
                        const y = height / 2 + height * (0.09 * Math.sin(t * Math.PI * 4)
                                  + spread[channel] * 0.25 * Math.sin(t * Math.PI * 2))
                        if (step === 0) c.moveTo(t * root.width, y)
                        else c.lineTo(t * root.width, y)
                    }
                    c.stroke()
                }
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onVisibleChanged: { if (visible) requestPaint() }
    }

    NumberAnimation on phase {
        running: root.motionEnabled
        from: 0
        to: 1
        duration: 5600
        loops: Animation.Infinite
    }
}
