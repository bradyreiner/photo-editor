import QtQuick

Item {
    id: root

    property int duration: 560
    property real intensity: 0.82
    property bool reducedMotion: theme.reducedMotion
    property real phase: 1
    readonly property bool running: sweep.running
    property int completionCount: 0
    readonly property bool surfaceAllocated: light.item !== null
    property int paintCount: 0

    Theme { id: theme }

    clip: true
    visible: running
    enabled: parent ? parent.enabled : true

    // Call only after the owning controller has published a completed result.
    // There is no idle timer, texture, input handler, or image-render request.
    function complete() {
        if (!enabled || !parent || !parent.visible)
            return
        completionCount += 1
        if (reducedMotion)
            return
        sweep.restart()
    }

    onReducedMotionChanged: if (reducedMotion) sweep.stop()
    onEnabledChanged: if (!enabled) sweep.stop()
    onVisibleChanged: if (!visible && sweep.running) sweep.stop()
    onPhaseChanged: if (running) light.requestPaint()

    NumberAnimation {
        id: sweep
        target: root
        property: "phase"
        from: 0
        to: 1
        duration: root.duration
        easing.type: Easing.InOutSine
    }

    Loader {
        anchors.fill: parent
        active: root.running
        sourceComponent: Canvas {
            id: surface
            antialiasing: true
            renderTarget: Canvas.Image
            opacity: Math.pow(Math.sin(Math.PI * root.phase), 1.4) * root.intensity
            onAvailableChanged: if (available) requestPaint()
            onPaint: {
                root.paintCount += 1
                const ctx = getContext("2d")
                ctx.reset()
                if (width <= 0 || height <= 0)
                    return
                const p = root.phase
                // Three aligned optical facets converge without a wave texture.
                // Upper intersections add light; lower intersections multiply
                // idealized ink transmittance. Blend before fading the whole mark.
                const focus = Math.min(1, p / 0.72)
                const separation = Math.pow(1 - focus, 2) * width * 0.18
                const halfWidth = Math.min(30, width * 0.3)
                const halfHeight = Math.max(2, Math.floor(height * 0.42))
                const centerX = width * 0.5
                const centerY = height * 0.5
                const lights = [theme.lightRed, theme.lightGreen, theme.lightBlue]
                const inks = [theme.processCyan, theme.processMagenta, theme.processYellow]
                // Pixel-aligned scanlines keep the sloping facet edges crisp at
                // compact sizes without relying on backend blend-mode support.
                for (let y = -halfHeight; y < halfHeight; y += 1) {
                    const span = halfWidth * (1 - Math.abs(y + 0.5) / halfHeight)
                    if (span <= 0) continue
                    const additive = y < 0
                    const colors = additive ? lights : inks
                    const edges = []
                    for (let plate = 0; plate < 3; ++plate) {
                        const offset = (plate - 1) * separation
                        edges.push(centerX + offset - span)
                        edges.push(centerX + offset + span)
                    }
                    edges.sort(function(a, b) { return a - b })
                    for (let segment = 0; segment + 1 < edges.length; ++segment) {
                        const midpoint = (edges[segment] + edges[segment + 1]) / 2
                        let r = additive ? 0 : 1
                        let g = r
                        let b = r
                        let coverage = 0
                        for (let plate = 0; plate < 3; ++plate) {
                            const offset = (plate - 1) * separation
                            if (Math.abs(midpoint - centerX - offset) > span) continue
                            ++coverage
                            if (additive) {
                                r += colors[plate].r; g += colors[plate].g; b += colors[plate].b
                            } else {
                                r *= colors[plate].r; g *= colors[plate].g; b *= colors[plate].b
                            }
                        }
                        if (coverage === 0) continue
                        ctx.fillStyle = Qt.rgba(Math.min(1, r), Math.min(1, g), Math.min(1, b), 1)
                        ctx.fillRect(edges[segment], centerY + y,
                                     edges[segment + 1] - edges[segment], 1)
                    }
                }

            }
        }
        id: light
        function requestPaint() {
            if (item) item.requestPaint()
        }
    }
}
