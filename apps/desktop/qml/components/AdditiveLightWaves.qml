import QtQuick

Item {
    id: root
    property bool active: true
    property bool reducedMotion: false
    property real phase: 0
    readonly property bool motionEnabled: active && visible && !reducedMotion && !theme.reducedMotion
    implicitWidth: 360
    implicitHeight: 80
    clip: true
    Accessible.ignored: true
    Theme { id: theme }

    function requestVisibleTexturePaint() {
        if (root.visible)
            lightTexture.requestPaint()
    }

    onVisibleChanged: {
        if (visible)
            lightTexture.requestPaint()
    }

    // Paint a repeating texture only on resize; motion translates the texture.
    // RGB light adds at intersections to produce cyan, magenta, yellow and white.
    Canvas {
        id: lightTexture
        width: root.width * 2
        height: root.height
        x: 0
        onPaint: {
            if (!root.visible || root.width <= 0 || height <= 0) return
            const c = getContext("2d")
            c.reset()
            const colors = ["#d9a08d", "#a8d6d4", "#8eaecd"]
            const spread = [-1, 0, 1]
            c.globalCompositeOperation = "lighter"
            for (let channel = 0; channel < 3; ++channel) {
                c.strokeStyle = colors[channel]
                c.shadowColor = colors[channel]
                c.shadowBlur = Math.min(16, height * 0.16)
                for (let halo = 0; halo < 2; ++halo) {
                    c.globalAlpha = halo === 0 ? 0.035 : 0.36
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
        onWidthChanged: root.requestVisibleTexturePaint()
        onHeightChanged: root.requestVisibleTexturePaint()
        onVisibleChanged: { if (visible) root.requestVisibleTexturePaint() }
    }
    XAnimator {
        target: lightTexture
        running: root.motionEnabled
        from: 0
        to: -root.width
        duration: 7200
        loops: Animation.Infinite
    }
}
